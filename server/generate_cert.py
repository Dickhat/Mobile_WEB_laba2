"""Create a self-signed HTTPS server certificate with the LAN IP in SAN.
Requires cryptography (installed by requirements.txt); works on Windows too.
"""
import argparse
from datetime import datetime, timedelta, timezone
from ipaddress import ip_address
from pathlib import Path
from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import rsa
from cryptography.x509.oid import NameOID, ExtendedKeyUsageOID

p = argparse.ArgumentParser()
p.add_argument('--ip', required=True, help='IPv4/IPv6 address of PC in Wi-Fi/LAN')
p.add_argument('--days', type=int, default=30)
p.add_argument('--force', action='store_true', help='Regenerate cert; pin must be updated on Aurora')
a = p.parse_args()
addr = ip_address(a.ip)
root = Path(__file__).resolve().parent / 'certs'
root.mkdir(exist_ok=True)
certfile, keyfile = root / 'server.crt', root / 'server.key'
if (certfile.exists() or keyfile.exists()) and not a.force:
    raise SystemExit('Certificate files exist. Use --force only if clients update the fingerprint.')
now = datetime.now(timezone.utc)
key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
name = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, 'Aurora Tasks Lab2')])
cert = (x509.CertificateBuilder().subject_name(name).issuer_name(name)
        .public_key(key.public_key()).serial_number(x509.random_serial_number())
        .not_valid_before(now - timedelta(minutes=5))
        .not_valid_after(now + timedelta(days=a.days))
        .add_extension(x509.SubjectAlternativeName([x509.IPAddress(addr),
                                                     x509.DNSName('localhost')]), critical=False)
        .add_extension(x509.BasicConstraints(ca=False, path_length=None), critical=True)
        .add_extension(x509.ExtendedKeyUsage([ExtendedKeyUsageOID.SERVER_AUTH]), critical=False)
        .sign(key, hashes.SHA256()))
keyfile.write_bytes(key.private_bytes(serialization.Encoding.PEM,
                                      serialization.PrivateFormat.TraditionalOpenSSL,
                                      serialization.NoEncryption()))
certfile.write_bytes(cert.public_bytes(serialization.Encoding.PEM))
print('Certificate:', certfile)
print('Private key (do not distribute):', keyfile)
print('SHA256 DER fingerprint:', cert.fingerprint(hashes.SHA256()).hex().upper())
print('Copy the SHA256 fingerprint into the native Aurora client.')
