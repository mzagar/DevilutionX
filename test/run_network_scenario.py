#!/usr/bin/env python3
"""Run a real TCP multiplayer scenario with the no-UI `network_peer` harness.

The host is started first and must report HOST_READY (which means its local
self-join through the real tcp_server/tcp_client pair succeeded) before any
client is launched. Each peer writes structured JSONL to its own result file;
the last line of a peer is always its RESULT.

This is the desktop orchestrator from the Wii U multiplayer handoff. The same
binary is used on the console, where a network_peer.ini on the SD card selects
the role instead of a command line.

Examples:

    python3 test/run_network_scenario.py --peer build-net/network_peer
    python3 test/run_network_scenario.py --peer build-net/network_peer \
        --players 4 --rounds 512 --port 6113
    python3 test/run_network_scenario.py --peer build-net/network_peer \
        --scenario wrong-password
    python3 test/run_network_scenario.py --peer build-net/network_peer \
        --scenario drop
"""

import argparse
import json
import os
import shutil
import signal
import subprocess
import sys
import tempfile
import time

READY_TIMEOUT = 30.0
LEAVE_TIMEOUT = 30.0


class PeerProcess:
    def __init__(self, name, argv, result_path, log_path):
        self.name = name
        self.argv = argv
        self.result_path = result_path
        self.log_path = log_path
        self.log_file = open(log_path, "wb")
        self.proc = subprocess.Popen(argv, stdout=self.log_file, stderr=subprocess.STDOUT)
        self.exit_code = None

    def poll(self):
        self.exit_code = self.proc.poll()
        return self.exit_code

    def wait(self, timeout):
        try:
            self.exit_code = self.proc.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            self.exit_code = None
        return self.exit_code

    def kill(self):
        if self.proc.poll() is None:
            self.proc.send_signal(signal.SIGKILL)
            try:
                self.proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                pass

    def close(self):
        self.log_file.close()


def read_events(path):
    events = []
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as f:
            for line in f:
                line = line.strip()
                if not line:
                    continue
                try:
                    events.append(json.loads(line))
                except json.JSONDecodeError:
                    pass
    except FileNotFoundError:
        pass
    return events


def event_named(events, name):
    for event in reversed(events):
        if event.get("event") == name:
            return event
    return None


def wait_for_event(peers, predicate, timeout, description):
    deadline = time.time() + timeout
    while time.time() < deadline:
        for peer in peers:
            events = read_events(peer.result_path)
            if predicate(peer, events):
                return peer, events
            if peer.poll() is not None and event_named(events, "RESULT") is None:
                raise RuntimeError(
                    f"{peer.name} exited with {peer.exit_code} before {description}; see {peer.log_path}"
                )
        time.sleep(0.05)
    raise RuntimeError(f"timed out waiting for {description}")


def peer_command(peer_path, result_path, **kwargs):
    argv = [peer_path, f"--result={result_path}"]
    for key, value in kwargs.items():
        if value is None:
            continue
        key = key.replace("_", "-")
        argv.append(f"--{key}={value}")
    return argv


def summarize(events):
    interesting = {}
    for name in ("BOOT", "HOST_READY", "JOINED", "GAME_DATA", "MESSAGE_TEST", "TURN_TEST",
                 "BYE", "LEAVE_EVENTS", "CLEANUP", "RESULT"):
        event = event_named(events, name)
        if event is not None:
            interesting[name] = event
    return interesting


def run(args):
    peer_path = os.path.abspath(args.peer)
    if not os.path.isfile(peer_path):
        raise SystemExit(f"peer binary not found: {peer_path} "
                         f"(configure with -DBUILD_NETWORK_HARNESS=ON)")

    out_dir = os.path.abspath(args.out)
    os.makedirs(out_dir, exist_ok=True)

    port = args.port
    players = args.players
    rounds = args.rounds
    password = None if args.scenario == "public" else args.password
    scenario_tag = f"{args.scenario}-{players}p"

    peers = []
    try:
        host_result = os.path.join(out_dir, "host.jsonl")
        host = PeerProcess(
            "host",
            peer_command(peer_path, host_result, role="host", players=players, port=port,
                         bind="127.0.0.1", rounds=rounds, scenario=scenario_tag,
                         password=("-" if password is None else password),
                         deadline=(20 if args.scenario == "wrong-password" else args.timeout),
                         expect_drop=(1 if args.scenario == "drop" else None)),
            host_result,
            os.path.join(out_dir, "host.log"),
        )
        peers.append(host)

        wait_for_event(peers, lambda p, e: event_named(e, "HOST_READY") is not None,
                       READY_TIMEOUT, "HOST_READY")
        print(f"[host] ready on 127.0.0.1:{port}")

        for index in range(1, players):
            result_path = os.path.join(out_dir, f"client{index}.jsonl")
            client_password = password
            if args.scenario == "wrong-password" and index == 1:
                client_password = "definitely-wrong"
            client = PeerProcess(
                f"client{index}",
                peer_command(peer_path, result_path, role="join", players=players, port=port,
                             connect=f"127.0.0.1:{port}", rounds=rounds,
                             scenario=scenario_tag,
                             password=("-" if client_password is None else client_password),
                             deadline=args.timeout,
                             drop_after=(args.drop_after if args.scenario == "drop" and index == 1 else None)),
                result_path,
                os.path.join(out_dir, f"client{index}.log"),
            )
            peers.append(client)

        deadline = time.time() + args.timeout + 30
        while time.time() < deadline:
            completed = 0
            for peer in peers:
                events = read_events(peer.result_path)
                if event_named(events, "RESULT") is not None:
                    completed += 1
                elif args.scenario == "drop" and peer.name != "host" and peer.proc.poll() is not None:
                    # The abruptly killed peer is expected not to produce a RESULT.
                    completed += 1
            if completed == len(peers):
                break
            time.sleep(0.05)

        for peer in peers:
            peer.kill()

        failures = []
        for peer in peers:
            events = read_events(peer.result_path)
            result = event_named(events, "RESULT")
            summary = summarize(events)
            print(f"\n[{peer.name}] exit={peer.exit_code}")
            for name, event in summary.items():
                print(f"  {name}: {json.dumps({k: v for k, v in event.items() if k not in ('event', 'ts')})}")

            expected_ok = True
            if args.scenario == "wrong-password" and peer.name == "client1":
                expected_ok = result is None or result.get("status") != "PASS"
                if result is not None and result.get("status") == "PASS":
                    failures.append(f"{peer.name} joined a game with a wrong password")
            elif args.scenario == "wrong-password" and peer.name == "host":
                # The host cannot finish: its only peer never joins.
                if result is not None and result.get("status") == "PASS":
                    failures.append("host reported PASS although the only client was rejected")
            elif args.scenario == "drop" and peer.name == "host":
                leave = event_named(events, "LEAVE_EVENTS")
                drop_seen = leave is not None and any(
                    entry.get("reason") == "DROP" for entry in leave.get("events", []))
                if not drop_seen:
                    failures.append("host did not report a DROP leave event")
            elif args.scenario == "drop" and peer.name != "host":
                if result is not None and result.get("status") == "PASS":
                    failures.append(f"{peer.name} reported PASS although it was supposed to abort")
            else:
                if result is None:
                    failures.append(f"{peer.name} produced no RESULT (log: {peer.log_path})")
                elif result.get("status") != "PASS":
                    failures.append(f"{peer.name} failed: {result.get('reason')}")


        return failures
    finally:
        for peer in peers:
            peer.kill()
            peer.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--peer", default="build-net/network_peer", help="path to the network_peer binary")
    parser.add_argument("--players", type=int, default=2, help="total players including the host")
    parser.add_argument("--rounds", type=int, default=1024, help="turn rounds (crosses the 8-bit sequence wrap)")
    parser.add_argument("--port", type=int, default=6112)
    parser.add_argument("--password", default="secret")
    parser.add_argument("--scenario", choices=["public", "private", "wrong-password", "drop"], default="public")
    parser.add_argument("--drop-after", type=int, default=32, help="rounds before a peer aborts abruptly")
    parser.add_argument("--timeout", type=int, default=120, help="per-peer deadline in seconds")
    parser.add_argument("--out", default="", help="result directory (default: a temporary directory)")
    parser.add_argument("--keep", action="store_true", help="keep the temporary result directory")
    args = parser.parse_args()

    temporary = not args.out
    if temporary:
        args.out = tempfile.mkdtemp(prefix="dvlnet-scenario-")

    failures = run(args)
    if failures:
        print("\nSCENARIO FAILED:")
        for failure in failures:
            print(f"  - {failure}")
        print(f"results kept in {args.out}")
        return 1

    print("\nSCENARIO PASSED")
    if temporary and not args.keep:
        shutil.rmtree(args.out, ignore_errors=True)
    else:
        print(f"results in {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
