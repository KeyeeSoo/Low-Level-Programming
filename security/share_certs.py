#!/usr/bin/env python3
"""Hand the host's DDS Security files to the other laptops on the same network.

  python3 security/share_certs.py serve   # host: answer requests until killed
  python3 security/share_certs.py fetch   # joiner: download, then exit

Uses UDP multicast on the local network, the same way DDS discovery finds the
server, so it works on any network where the game itself works. Only the files
a *client* needs are sent: server.key never leaves the host, so a joining
laptop cannot pretend to be the server.
"""
import base64, hashlib, json, os, secrets, shutil, socket, struct, sys, time, zlib

GROUP, PORT = "239.255.0.77", 7477
CLIENT_FILES = ["ca.pem", "client.pem", "client.key", "governance.p7s", "permissions.p7s"]
HERE = os.path.dirname(os.path.abspath(__file__))
CERT_DIR = os.path.join(HERE, "generated")
CHUNK = 1000  # stays under one Wi-Fi frame, so nothing depends on IP fragmentation
FETCH_TIMEOUT_S = 120


def local_ip():
    """The address of the interface that leads to the rest of the network."""
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("192.0.2.1", 9))  # no packet is sent; this only picks a route
        return s.getsockname()[0]
    except OSError:
        return "0.0.0.0"
    finally:
        s.close()


def open_socket():
    ip = local_ip()
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    if hasattr(socket, "SO_REUSEPORT"):
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
    s.bind(("", PORT))
    s.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP,
                 struct.pack("4s4s", socket.inet_aton(GROUP), socket.inet_aton(ip)))
    s.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_IF, socket.inet_aton(ip))
    s.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, 1)
    return s, ip


def fingerprint(path):
    with open(path, "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()[:16]


def serve():
    files = {}
    for name in CLIENT_FILES:
        with open(os.path.join(CERT_DIR, name), "rb") as f:
            files[name] = base64.b64encode(f.read()).decode()
    payload = zlib.compress(json.dumps(files).encode())
    digest = hashlib.sha256(payload).hexdigest()
    chunks = [payload[i:i + CHUNK] for i in range(0, len(payload), CHUNK)]
    s, ip = open_socket()
    print(f"[certs] sharing on {ip} (CA {fingerprint(os.path.join(CERT_DIR, 'ca.pem'))})", flush=True)
    while True:
        data, addr = s.recvfrom(2048)
        if not data.startswith(b"GBREQ1 "):
            continue
        nonce = data[7:].strip().decode(errors="replace")[:32]
        print(f"[certs] sending to {addr[0]}", flush=True)
        for i, part in enumerate(chunks):
            head = f"GBDAT1 {nonce} {i} {len(chunks)} {digest}\n".encode()
            s.sendto(head + part, (GROUP, PORT))
            time.sleep(0.002)


def fetch():
    s, ip = open_socket()
    s.settimeout(1.0)
    nonce = secrets.token_hex(8)
    parts, total, digest = {}, None, None
    deadline = time.time() + FETCH_TIMEOUT_S
    print(f"Looking for the host on the network (this laptop is {ip})...", flush=True)
    last_req = 0.0
    while time.time() < deadline:
        if time.time() - last_req > 1.0:
            s.sendto(f"GBREQ1 {nonce}".encode(), (GROUP, PORT))
            last_req = time.time()
        try:
            data, _ = s.recvfrom(4096)
        except socket.timeout:
            continue
        if not data.startswith(b"GBDAT1 "):
            continue
        head, _, body = data.partition(b"\n")
        fields = head.decode(errors="replace").split()
        if len(fields) != 5 or fields[1] != nonce:
            continue
        idx, total, digest = int(fields[2]), int(fields[3]), fields[4]
        parts[idx] = body
        if len(parts) == total:
            payload = b"".join(parts[i] for i in range(total))
            if hashlib.sha256(payload).hexdigest() != digest:
                parts.clear()  # damaged in transit: ask again
                continue
            install(json.loads(zlib.decompress(payload)))
            return 0
    print("Could not find the host. On Player 1's laptop, start run.bat and pick Host,\n"
          "and make sure both laptops are on the same Wi-Fi or hotspot.", file=sys.stderr)
    return 1


def install(files):
    tmp = CERT_DIR + ".tmp"
    shutil.rmtree(tmp, ignore_errors=True)
    os.makedirs(tmp)
    for name in CLIENT_FILES:
        with open(os.path.join(tmp, name), "wb") as f:
            f.write(base64.b64decode(files[name]))
    shutil.rmtree(CERT_DIR, ignore_errors=True)
    os.rename(tmp, CERT_DIR)
    print(f"Got the host's certificates (CA {fingerprint(os.path.join(CERT_DIR, 'ca.pem'))}).", flush=True)


if __name__ == "__main__":
    mode = sys.argv[1] if len(sys.argv) > 1 else ""
    if mode == "serve":
        serve()
    elif mode == "fetch":
        sys.exit(fetch())
    else:
        sys.exit(__doc__)
