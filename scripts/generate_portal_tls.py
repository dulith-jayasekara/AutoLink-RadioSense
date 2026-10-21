from datetime import datetime, timezone
import base64
from ipaddress import ip_address
from pathlib import Path
import uuid

from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.x509.oid import ExtendedKeyUsageOID, NameOID

root = Path(__file__).resolve().parents[1]
secret_dir = root / ".secrets"
secret_dir.mkdir(exist_ok=True)
key_path = secret_dir / "portal_root_ca_key.pem"
ca_path = root / "src" / "Core" / "PortalRootCA.crt"
header_path = root / "src" / "Core" / "PortalTlsCertificate.h"

if key_path.exists() and ca_path.exists():
    ca_key = serialization.load_pem_private_key(key_path.read_bytes(), password=None)
    ca_cert = x509.load_pem_x509_certificate(ca_path.read_bytes())
else:
    ca_key = ec.generate_private_key(ec.SECP256R1())
    name = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, "AutoLink RadioSense Portal CA")])
    ca_cert = (
        x509.CertificateBuilder()
        .subject_name(name)
        .issuer_name(name)
        .public_key(ca_key.public_key())
        .serial_number(x509.random_serial_number())
        .not_valid_before(datetime(2020, 1, 1, tzinfo=timezone.utc))
        .not_valid_after(datetime(2045, 1, 1, tzinfo=timezone.utc))
        .add_extension(x509.BasicConstraints(ca=True, path_length=0), critical=True)
        .add_extension(x509.KeyUsage(digital_signature=True, content_commitment=False,
            key_encipherment=False, data_encipherment=False, key_agreement=False,
            key_cert_sign=True, crl_sign=True, encipher_only=False, decipher_only=False), critical=True)
        .sign(ca_key, hashes.SHA256())
    )
    key_path.write_bytes(ca_key.private_bytes(
        serialization.Encoding.PEM,
        serialization.PrivateFormat.PKCS8,
        serialization.NoEncryption(),
    ))
    ca_path.write_bytes(ca_cert.public_bytes(serialization.Encoding.PEM))

server_key = ec.generate_private_key(ec.SECP256R1())
server_name = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, "192.168.4.1")])
server_cert = (
    x509.CertificateBuilder()
    .subject_name(server_name)
    .issuer_name(ca_cert.subject)
    .public_key(server_key.public_key())
    .serial_number(x509.random_serial_number())
    .not_valid_before(datetime(2020, 1, 1, tzinfo=timezone.utc))
    .not_valid_after(datetime(2035, 1, 1, tzinfo=timezone.utc))
    .add_extension(x509.SubjectAlternativeName([x509.IPAddress(ip_address("192.168.4.1"))]), critical=False)
    .add_extension(x509.BasicConstraints(ca=False, path_length=None), critical=True)
    .add_extension(x509.ExtendedKeyUsage([ExtendedKeyUsageOID.SERVER_AUTH]), critical=False)
    .sign(ca_key, hashes.SHA256())
)

def array(name, payload):
    values = ", ".join(f"0x{value:02x}" for value in payload)
    return f"static const uint8_t {name}[] = {{{values}}};\n"

cert_der = server_cert.public_bytes(serialization.Encoding.DER)
key_der = server_key.private_bytes(
    serialization.Encoding.DER,
    serialization.PrivateFormat.PKCS8,
    serialization.NoEncryption(),
)
ca_pem = ca_cert.public_bytes(serialization.Encoding.PEM).decode("ascii")
ca_der = ca_cert.public_bytes(serialization.Encoding.DER)
ca_literal = "R\"CERT(" + ca_pem + ")CERT\""
fingerprint = ca_cert.fingerprint(hashes.SHA256()).hex(":").upper()
profile_id = str(uuid.uuid4()).upper()
profile_uuid = str(uuid.uuid4()).upper()
profile = f'''<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
<key>PayloadContent</key><array><dict>
<key>PayloadContent</key><data>{base64.b64encode(ca_der).decode("ascii")}</data>
<key>PayloadDescription</key><string>AutoLink RadioSense portal certificate authority</string>
<key>PayloadDisplayName</key><string>AutoLink RadioSense Portal CA</string>
<key>PayloadIdentifier</key><string>org.autolink.radiosense.portalca</string>
<key>PayloadType</key><string>com.apple.security.root</string>
<key>PayloadUUID</key><string>{profile_id}</string>
<key>PayloadVersion</key><integer>1</integer>
</dict></array>
<key>PayloadDisplayName</key><string>AutoLink RadioSense Portal Certificate</string>
<key>PayloadIdentifier</key><string>org.autolink.radiosense.portal</string>
<key>PayloadOrganization</key><string>AutoLink RadioSense</string>
<key>PayloadRemovalDisallowed</key><false/>
<key>PayloadType</key><string>Configuration</string>
<key>PayloadUUID</key><string>{profile_uuid}</string>
<key>PayloadVersion</key><integer>1</integer>
</dict></plist>'''
profile_literal = 'R"PROFILE(' + profile + ')PROFILE"'

header = "#pragma once\n#include <Arduino.h>\n\n"
header += f'#define PORTAL_CA_FINGERPRINT "{fingerprint}"\n\n'
header += array("PORTAL_SERVER_CERT_DER", cert_der) + "\n"
header += array("PORTAL_SERVER_KEY_DER", key_der) + "\n"
header += "static const char PORTAL_CA_CERT_PEM[] PROGMEM = " + ca_literal + ";\n"
header += "static const char PORTAL_CA_MOBILECONFIG[] PROGMEM = " + profile_literal + ";\n"
header_path.write_text(header, encoding="ascii")
print(f"Wrote {header_path}; CA fingerprint SHA-256: {fingerprint}")
