"""Local two-node RadioSense test dashboard. Python standard library only."""
import argparse
from concurrent.futures import ThreadPoolExecutor
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import ipaddress
import json
from pathlib import Path
import re
import socketserver
import threading
import time
import urllib.error
import urllib.parse
import urllib.request

ROOT = Path(__file__).resolve().parent
DEFAULT_NODES = [
    {'key': 'a', 'name': 'Node A', 'expected_id': '669563705', 'ip': ''},
    {'key': 'b', 'name': 'Node B', 'expected_id': '1747025710', 'ip': ''},
]
FIELDS = re.compile(r'([a-zA-Z_]+)=([^\s]+)')


class LocalServer(ThreadingHTTPServer):
    def server_bind(self):
        # Loopback needs no reverse DNS lookup (which can stall on offline Wi-Fi).
        socketserver.TCPServer.server_bind(self)
        self.server_name = 'localhost'
        self.server_port = self.server_address[1]


def fields(line):
    # Keep identifiers as strings: 64-bit hazard IDs exceed JavaScript's exact integer range.
    return dict(FIELDS.findall(line))


def parse_status(text):
    result = {'info': {}, 'memory': {}, 'sync': {}, 'hazards': [], 'groups': [], 'raw': text}
    for line in text.splitlines():
        values = fields(line)
        if line.startswith('node='):
            result['info'].update(values)
        elif line.startswith(('records=', 'observations=', 'memory_policy=', 'retention_policy=')):
            result['memory'].update(values)
        elif line.startswith('sync '):
            result['sync'].update(values)
        elif line.startswith('id='):
            result['hazards'].append(values)
        elif line.startswith('group='):
            result['groups'].append(values)
    if not result['info'].get('node') or 'records' not in result['memory'] or not result['sync']:
        raise ValueError('Incomplete node status response; check firmware and address')
    return result


def validate_ip(value):
    if not value:
        return ''
    address = ipaddress.IPv4Address(value)
    ranges = ('10.0.0.0/8', '172.16.0.0/12', '192.168.0.0/16')
    if not any(address in ipaddress.IPv4Network(network) for network in ranges):
        raise ValueError('Use the ESP32 private router IPv4 address')
    return str(address)


def firmware_compatibility(nodes):
    boards = []
    for node in nodes:
        memory = node.get('memory', {})
        ready = (node.get('online') and memory.get('sync_protocol') == '5'
                 and memory.get('conflict_policy') == '1' and memory.get('memory_policy') == '1' and memory.get('retention_policy') == '1')
        boards.append({'key': node.get('key'), 'name': node.get('name', 'Node'),
                       'online': bool(node.get('online')), 'ready': bool(ready),
                       'sync_protocol': memory.get('sync_protocol'),
                       'conflict_policy': memory.get('conflict_policy'), 'memory_policy': memory.get('memory_policy'), 'retention_policy': memory.get('retention_policy')})
    if len(boards) != 2 or not all(board['online'] for board in boards):
        state, message = 'unavailable', 'Connect both nodes to verify firmware features.'
    elif all(board['ready'] for board in boards):
        state, message = 'ready', 'Both nodes support memory, conflict resolution and retention diagnostics (protocol 5, retention policy 1).'
    else:
        state, message = 'upgrade_required', ('Install the latest protocol 5 firmware with retention policy 1 on both ESP32s before '
                                              'testing retention. Existing data stays intact.')
    return {'state': state, 'message': message, 'nodes': boards}


def compare_records(nodes):
    if len(nodes) != 2 or not all(node.get('online') for node in nodes):
        return {'state': 'unavailable', 'message': 'Connect both nodes to compare their records.',
                'different_ids': [], 'details': []}
    columns = ('id', 'origin', 'updater', 'type', 'severity', 'lat', 'lon', 'created',
               'observed', 'fix', 'accuracy', 'expires', 'version', 'confidence', 'status')
    records = [{r['id']: r for r in n['hazards']} for n in nodes]
    details = []
    for hazard in sorted(records[0].keys() | records[1].keys()):
        a, b = records[0].get(hazard), records[1].get(hazard)
        if a is None or b is None:
            details.append({'id': hazard, 'kind': 'missing_a' if a is None else 'missing_b',
                            'fields': [], 'a': a, 'b': b})
            continue
        changed = [column for column in columns if a.get(column) != b.get(column)]
        if changed:
            details.append({'id': hazard, 'kind': 'field_difference', 'fields': changed, 'a': a, 'b': b})
    differences = [item['id'] for item in details]
    return {'state': 'different' if differences else 'matching', 'different_ids': differences,
            'details': details,
            'message': ('Stored records differ for ' + str(len(differences)) + ' hazard(s). See the fields below.')
            if differences else 'Both nodes have matching stored records. Evidence is inspected separately.'}


def parse_retention(text):
    result = {'summary': {}, 'records': [], 'markers': [], 'raw': text}
    for line in text.splitlines():
        if line.startswith('retention_policy='):
            result['summary'].update(fields(line))
        elif line.startswith('retention id='):
            result['records'].append(fields(line))
        elif line.startswith('retired='):
            result['markers'].append(fields(line))
    summary = result['summary']
    required = ('utc', 'history_seconds', 'marker_expiry', 'full_terminal_count', 'eligible_count',
                'cleanup_ready', 'cleanup_blocked', 'marker_count', 'free_marker_slots',
                'retired_record_blocks', 'retired_evidence_blocks')
    if summary.get('retention_policy') != '1' or any(key not in summary for key in required):
        raise ValueError('Incomplete retention response; update the board firmware')
    seen = set()
    for item, key in [(row, 'id') for row in result['records']] + [(row, 'retired') for row in result['markers']]:
        value = item.get(key, '')
        if not value.isascii() or not value.isdecimal() or not 0 < int(value) < 2 ** 64 or value in seen:
            raise ValueError('Invalid or duplicate retention identity')
        if not item.get('created') or not item.get('status'):
            raise ValueError('Incomplete terminal identity')
        if key == 'id' and any(field not in item for field in ('expires', 'state', 'deadline', 'remaining_seconds')):
            raise ValueError('Incomplete retention timing')
        seen.add(value)
    if summary['marker_count'] != str(len(result['markers'])):
        raise ValueError('Incomplete retirement-marker list')
    return result


def compare_retention(nodes):
    if len(nodes) != 2 or not all(n.get('online') for n in nodes):
        return {'state': 'unavailable', 'message': 'Connect both nodes to compare terminal protection.', 'details': []}
    if not all(n.get('supported') for n in nodes):
        return {'state': 'unavailable', 'message': 'Install retention diagnostics on both boards to compare replay protection.', 'details': []}
    identities = []
    for node in nodes:
        protected = {}
        for row in node['records']:
            if row['status'].split('(')[0] in ('2', '3'):
                protected[row['id']] = dict(created=row['created'], status=row['status'], storage='Full history')
        for row in node['markers']:
            protected[row['retired']] = dict(created=row['created'], status=row['status'], storage='Compact marker')
        identities.append(protected)
    differences = []
    for hazard in sorted(identities[0].keys() | identities[1].keys()):
        a, b = identities[0].get(hazard), identities[1].get(hazard)
        if a is None or b is None or (a['created'], a['status']) != (b['created'], b['status']):
            differences.append({'id': hazard, 'a': a, 'b': b})
    return {'state': 'different' if differences else 'matching', 'details': differences,
            'message': 'Terminal protection differs; allow LoRa exchange, then refresh.' if differences else
            'Both nodes protect matching terminal identities. Full history and compact storage may differ.'}


class Monitor:
    def __init__(self, config):
        self.config = config
        self.guard = threading.Lock()
        self.locks = {key: threading.Lock() for key in ('a', 'b')}
        self.nodes = [dict(n) for n in DEFAULT_NODES]
        if config.exists():
            self.configure(json.loads(config.read_text()), save=False)

    def configure(self, nodes, save=True):
        if not isinstance(nodes, list) or len(nodes) != 2:
            raise ValueError('Configure exactly two nodes')
        updated = []
        for default, node in zip(DEFAULT_NODES, nodes):
            if not isinstance(node, dict) or node.get('key') != default['key']:
                raise ValueError('Node keys must be a and b in that order')
            updated.append(dict(default, ip=validate_ip(node.get('ip', '').strip())))
        if updated[0]['ip'] and updated[0]['ip'] == updated[1]['ip']:
            raise ValueError('Each node needs its own router IP address')
        with self.guard:
            if save:
                self.config.parent.mkdir(parents=True, exist_ok=True)
                temporary = self.config.with_suffix('.tmp')
                temporary.write_text(json.dumps(updated, indent=2) + '\n')
                temporary.replace(self.config)
            self.nodes = updated

    def configured(self):
        with self.guard:
            return [dict(n) for n in self.nodes]

    def request(self, node, path, command=None):
        data = None if command is None else urllib.parse.urlencode({'command': command}).encode()
        req = urllib.request.Request('http://' + node['ip'] + path, data=data,
                                     headers={'X-RadioSense': '1', 'X-RadioSense-Node': node['expected_id']})
        # Do not use internet proxies or follow redirects away from the configured node.
        class NoRedirect(urllib.request.HTTPRedirectHandler):
            def redirect_request(self, *args, **kwargs):
                return None
        opener = urllib.request.build_opener(urllib.request.ProxyHandler({}), NoRedirect())
        try:
            response = opener.open(req, timeout=3)
        except urllib.error.HTTPError as error:
            error.close()
            raise
        with response:
            body = response.read(16385)
            if len(body) > 16384:
                raise ValueError('Node response exceeded its expected bound')
            return body.decode('utf-8')

    def checked_status(self, node):
        status = parse_status(self.request(node, '/api/status'))
        if status['info']['node'] != node['expected_id']:
            raise ValueError('Wrong board at this IP: expected ' + node['expected_id'] +
                             ', received ' + status['info']['node'])
        return status

    def snapshot_node(self, node):
        result = dict(node, online=False, sampled_at=time.time())
        if not node['ip']:
            return dict(result, error='Enter this board’s router IP above.')
        try:
            with self.locks[node['key']]:
                status = self.checked_status(node)
                try:
                    status['network'] = json.loads(self.request(node, '/api/network'))
                except (urllib.error.URLError, ValueError, TimeoutError, OSError):
                    status['network'] = {'connected': None, 'station_ip': node['ip']}
            result.update(status, online=True)
        except (urllib.error.URLError, ValueError, TimeoutError, OSError) as error:
            result['error'] = str(error)
        return result

    def snapshot(self):
        with ThreadPoolExecutor(max_workers=2) as pool:
            nodes = list(pool.map(self.snapshot_node, self.configured()))
        return {'nodes': nodes, 'comparison': compare_records(nodes),
                'compatibility': firmware_compatibility(nodes), 'sampled_at': time.time()}

    def retention_node(self, node):
        result = dict(node, online=False, supported=False, sampled_at=time.time())
        if not node['ip']:
            return dict(result, error='Configure this board first.')
        try:
            with self.locks[node['key']]:
                status = self.checked_status(node)
                result['online'] = True
                if status['memory'].get('retention_policy') != '1':
                    return dict(result, error='Install the retention firmware on this board. Existing data stays intact.')
                result.update(parse_retention(self.request(node, '/api/command', 'RETENTION')), supported=True)
        except (urllib.error.URLError, ValueError, TimeoutError, OSError) as error:
            result['error'] = str(error)
        return result

    def retention(self):
        with ThreadPoolExecutor(max_workers=2) as pool:
            nodes = list(pool.map(self.retention_node, self.configured()))
        return {'nodes': nodes, 'comparison': compare_retention(nodes), 'sampled_at': time.time()}

    def command(self, key, command):
        if not isinstance(command, str) or not command or len(command.encode()) > 191 or any(
                ch in command for ch in ('\n', '\r', '\0')):
            raise ValueError('Enter one command of 1 to 191 bytes')
        node = next((n for n in self.configured() if n['key'] == key), None)
        if node is None or not node['ip']:
            raise ValueError('Configure the selected node first')
        with self.locks[key]:
            self.checked_status(node)  # Fail before commands if node identities are swapped.
            return self.request(node, '/api/command', command)


def handler_for(monitor, local_key=None):
    class Handler(BaseHTTPRequestHandler):
        def send(self, code, body, content_type='application/json'):
            data = (json.dumps(body) if content_type == 'application/json' else body).encode()
            self.send_response(code)
            self.send_header('Content-Type', content_type + '; charset=utf-8')
            self.send_header('Content-Length', str(len(data)))
            self.send_header('Cache-Control', 'no-store')
            self.send_header('X-Content-Type-Options', 'nosniff')
            self.send_header('X-Frame-Options', 'DENY')
            self.end_headers()
            self.wfile.write(data)

        def do_GET(self):
            path = urllib.parse.urlsplit(self.path)
            try:
                if path.path == '/':
                    self.send(200, (ROOT / ('laptop.html' if local_key else 'device_setup.html')).read_text(), 'text/html')
                elif path.path.startswith('/assets/'):
                    files = {'leaflet.js': 'application/javascript', 'leaflet.css': 'text/css',
                             'laptop_map.js': 'application/javascript', 'LICENSE': 'text/plain'}
                    name = path.path.removeprefix('/assets/')
                    if name not in files:
                        self.send(404, {'error': 'Not found'})
                    else:
                        self.send(200, (ROOT / 'assets' / name).read_text(), files[name])
                elif path.path in ('/api/device/status', '/api/device/network'):
                    if not local_key:
                        raise ValueError('Start with --node a or --node b')
                    node = next(n for n in monitor.configured() if n['key'] == local_key)
                    with monitor.locks[local_key]:
                        status = monitor.checked_status(node)
                        if path.path.endswith('/status'):
                            self.send(200, status['raw'], 'text/plain')
                        else:
                            network = json.loads(monitor.request(node, '/api/network'))
                            if network.get('node') != node['expected_id']:
                                raise ValueError('Node identity changed')
                            self.send(200, network)
                elif path.path in ('/monitor', '/monitor/'):
                    self.send(200, (ROOT / 'index.html').read_text(), 'text/html')
                elif path.path in ('/demo', '/demo/'):
                    self.send(200, (ROOT / 'demo/index.html').read_text(), 'text/html')
                elif path.path == '/api/demo':
                    self.send(200, json.loads((ROOT / 'demo/firmware-demo.json').read_text()))
                elif path.path == '/api/nodes':
                    self.send(200, monitor.configured())
                elif path.path == '/api/snapshot':
                    self.send(200, monitor.snapshot())
                elif path.path == '/api/retention':
                    self.send(200, monitor.retention())
                elif path.path == '/api/retired':
                    query = urllib.parse.parse_qs(path.query)
                    text = monitor.command(query.get('node', [''])[0], 'RETIRED')
                    if 'INVALID_COMMAND' in text:
                        raise ValueError('This board needs the memory firmware update')
                    self.send(200, {'markers': [fields(line) for line in text.splitlines()
                                              if line.startswith('retired=')], 'raw': text})
                elif path.path == '/api/evidence':
                    query = urllib.parse.parse_qs(path.query)
                    hazard = query.get('hazard', [''])[0]
                    if not hazard.isascii() or not hazard.isdecimal() or not 0 < int(hazard) < 2 ** 64:
                        raise ValueError('Invalid hazard ID')
                    text = monitor.command(query.get('node', [''])[0], 'EVIDENCE ' + hazard)
                    self.send(200, {'observations': [fields(line) for line in text.splitlines()
                                                  if line.startswith('observation=')], 'raw': text})
                else:
                    self.send(404, {'error': 'Not found'})
            except ValueError as error:
                self.send(400, {'error': str(error)})
            except (urllib.error.URLError, TimeoutError, OSError) as error:
                self.send(502, {'error': str(error)})

        def do_POST(self):
            if self.headers.get('X-RadioSense') != '1':
                self.send(403, {'error': 'Use the local dashboard'})
                return
            try:
                length = int(self.headers.get('Content-Length', '0'))
                if not 0 < length <= 4096:
                    raise ValueError('Invalid request size')
                raw = self.rfile.read(length)
                if self.path == '/api/device/command':
                    if not local_key:
                        raise ValueError('Start with --node a or --node b')
                    node = next(n for n in monitor.configured() if n['key'] == local_key)
                    if self.headers.get('X-RadioSense-Node') != node['expected_id']:
                        self.send(409, 'WRONG_NODE; restart the app with the intended --node setting.', 'text/plain')
                        return
                    values = urllib.parse.parse_qs(raw.decode('utf-8'), keep_blank_values=True)
                    commands = values.get('command', [])
                    if len(commands) != 1:
                        raise ValueError('Provide exactly one command')
                    self.send(200, monitor.command(local_key, commands[0]), 'text/plain')
                    return
                data = json.loads(raw)
                if self.path == '/api/nodes':
                    monitor.configure(data)
                    self.send(200, monitor.configured())
                elif self.path == '/api/command':
                    if not isinstance(data, dict):
                        raise ValueError('Expected a command object')
                    self.send(200, {'output': monitor.command(data.get('node'), data.get('command'))})
                else:
                    self.send(404, {'error': 'Not found'})
            except (ValueError, TypeError, AttributeError) as error:
                self.send(400, {'error': str(error)})
            except (urllib.error.URLError, TimeoutError, OSError) as error:
                self.send(502, {'error': str(error)})

    return Handler


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', type=int, default=8765)
    parser.add_argument('--node', choices=('a', 'b'), default='a', help='Independent local laptop app for this board')
    parser.add_argument('--node-ip', default='192.168.4.1', help='Own ESP32 hotspot API address')
    parser.add_argument('--config', type=Path, default=ROOT / 'nodes.local.json')
    args = parser.parse_args()
    monitor = Monitor(args.config)
    if args.node:
        monitor.configure([dict(n, ip=validate_ip(args.node_ip) if n['key'] == args.node else '') for n in DEFAULT_NODES], save=False)
    server = LocalServer(('127.0.0.1', args.port), handler_for(monitor, args.node))
    server.daemon_threads = True
    print(f'RadioSense monitor: http://127.0.0.1:{server.server_port}', flush=True)
    print(('Join RadioSense-' + next(n['expected_id'] for n in DEFAULT_NODES if n['key'] == args.node) + '; the application runs on this laptop.' if args.node else 'Optional legacy tools. Use --node a or --node b for the local laptop application.'), flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == '__main__':
    main()
