"""Read-only HTTP checks against a powered node; join its Wi-Fi hotspot first."""
import argparse
import json
from pathlib import Path
import urllib.error
import urllib.parse
import urllib.request


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--url', default='http://192.168.4.1')
    parser.add_argument('--expect-node', required=True)
    parser.add_argument('--hazard-id')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
    results = []

    def request(path, command=None, header=True, expected_node=None):
        headers = {'X-RadioSense': '1'} if header else {}
        if header:
            headers['X-RadioSense-Node'] = expected_node or args.expect_node
        data = None if command is None else urllib.parse.urlencode({'command': command}).encode()
        req = urllib.request.Request(args.url.rstrip('/') + path, data=data, headers=headers)
        try:
            reply = opener.open(req, timeout=8)
        except urllib.error.HTTPError as error:
            reply = error
        with reply:
            limit = 65536 if path == '/' else 16384
            body = reply.read(limit + 1)
            assert len(body) <= limit, 'Oversized response'
            return reply.code, reply.headers, body.decode('utf-8')

    def check(name, condition):
        assert condition, name
        results.append(name)
        print('PASS:', name, flush=True)

    code, headers, body = request('/')
    check('API-only root served', code == 200 and json.loads(body)['service'] == 'RadioSense node API'
          and 'text/html' not in headers.get('Content-Type', ''))
    code, _, body = request('/api/network')
    network = json.loads(body)
    check('independent hotspot configuration', code == 200 and network['node'] == args.expect_node
          and network['mode'] == 'device_ap' and network['router_required'] is False
          and network['synchronization'] == 'lora' and network['ap_ip'] == '192.168.4.1')
    other_node = '2' if args.expect_node == '1' else '1'
    code, _, body = request('/api/command', 'LIST', expected_node=other_node)
    check('command for a different node rejected', code == 409 and 'WRONG_NODE' in body)
    code, headers, body = request('/api/status')
    check('expected node identity', code == 200 and f'node={args.expect_node} ' in body)
    check('live status includes storage, sync, records and groups', all(
        text in body for text in ('storage_writable=', 'sync sent=', 'matches=', 'groups=')))
    check('status caching disabled', headers.get('Cache-Control') == 'no-store')
    code, _, body = request('/api/command', 'HELP')
    check('command output captured over HTTP', code == 200 and 'REPORT_ID' in body and 'EVIDENCE' in body)
    code, _, body = request('/api/command', 'SYNC')
    check('sync command returns counters', code == 200 and 'sync sent=' in body)
    code, _, body = request('/api/command', 'INVALID_TEST_COMMAND')
    check('invalid command reaches existing parser', code == 200 and 'INVALID_COMMAND' in body)
    for name, command in [('empty', ''), ('multiline', 'LIST\nSYNC'),
                          ('carriage return', 'LIST\rSYNC'), ('NUL', 'LIST\0SYNC'),
                          ('overlong', 'x' * 192)]:
        code, _, _ = request('/api/command', command)
        check(name + ' command rejected', code == 400)
    code, _, _ = request('/api/command', 'LIST', header=False)
    check('command without custom header rejected', code == 403)
    code, _, _ = request('/missing-test-route')
    check('unknown route rejected', code == 404)
    if args.hazard_id:
        code, _, body = request('/api/command', 'GET ' + args.hazard_id)
        check('expected persisted hazard available', code == 200 and f'id={args.hazard_id} ' in body)
        code, _, body = request('/api/command', 'EVIDENCE ' + args.hazard_id)
        check('persisted evidence available', code == 200 and 'observation=' in body)
    if args.output:
        args.output.write_text(json.dumps({'node': args.expect_node, 'url': args.url,
                                         'checks_passed': results, 'mutations_sent': False}, indent=2) + '\n')


if __name__ == '__main__':
    try:
        main()
    except (urllib.error.URLError, TimeoutError, OSError) as error:
        raise SystemExit('HTTP check could not reach the node: ' + str(error) +
                         '. Join its RadioSense Wi-Fi hotspot, then retry.') from None
