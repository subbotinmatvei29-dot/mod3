# Задание 14 — снифер UDP-пакетов на RAW-сокете

## Что делает программа

Программа `sniffer` перехватывает входящие UDP-пакеты, адресованные указанному порту, и выводит:

- размер пакета;
- IP-адрес отправителя и получателя;
- порт отправителя и получателя;
- длину UDP-датаграммы;
- полезную нагрузку (payload), то есть текст сообщения.

В качестве источника трафика используется чат из задания 12 (`chat.c`), реализованный на UDP.

## Почему понадобилось исправление под macOS

В Linux и BSD/macOS отличаются имена структур и полей сетевых заголовков.

На macOS:
- IP-заголовок — `struct ip`
- поля: `ip_v`, `ip_hl`, `ip_p`, `ip_src`, `ip_dst`
- UDP-заголовок — `struct udphdr`
- поля: `uh_sport`, `uh_dport`, `uh_ulen`

В Linux в учебных примерах часто используют `struct iphdr` и Linux-имена полей.

Поэтому в проекте добавлена платформенная развилка через `#if defined(__APPLE__) ...`.

## Сборка

```bash
make


sudo ./sniffer 5001

python3 -c "import socket; s=socket.socket(socket.AF_INET, socket.SOCK_STREAM); s.sendto(b'hello', ('127.0.0.1', 5001)); s.close()"


SOCK_STREAM



для тср

python3 -c "import socket; s=socket.socket(); s.bind(('127.0.0.1',6001)); s.listen(1); c,addr=s.accept(); print('connected', addr); data=c.recv(1024); print(data); c.close(); s.close()"



python3 -c "import socket; s=socket.socket(); s.connect(('127.0.0.1',6001)); s.sendall(b'hello_tcp'); s.close()"



sudo apt update
sudo apt install -y build-essential
grep -n "pcap" sniffer.c
grep -n "pcap" Makefile
make clean
make
ls -l


sudo ./sniffer 5001


python3 -c "import socket; s=socket.socket(socket.AF_INET, socket.SOCK_DGRAM); s.sendto(b'hello', ('127.0.0.1', 5001)); s.close()"