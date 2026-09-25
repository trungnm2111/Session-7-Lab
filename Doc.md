# Lập trình Socket trên Linux

## Mục lục

- [Phần 1. Tổng quan](#phần-1-tổng-quan)
- [Phần 2. Địa chỉ socket và thứ tự byte](#phần-2-địa-chỉ-socket-và-thứ-tự-byte)
- [Phần 3. Các hàm `inet_aton`, `inet_pton`, `inet_ntop`](#phần-3-các-hàm-inet_aton-inet_pton-inet_ntop)
- [Phần 4. Các hàm socket cơ bản](#phần-4-các-hàm-socket-cơ-bản)
- [Phần 5. Server đồng thời (Concurrent Servers)](#phần-5-server-đồng-thời-concurrent-servers)
- [Phần 6. Các hàm `close`, `getsockname`, `getpeername`](#phần-6-các-hàm-close-getsockname-getpeername)

---

## Phần 1. Tổng quan

### 1.1. Độc lập giao thức (Protocol Independence)

Giao thức (protocol) là bộ quy tắc mà hai bên thống nhất để trao đổi dữ liệu, ví dụ IPv4, IPv6, TCP, UDP.

Một chương trình được gọi là **phụ thuộc giao thức** (protocol-dependent) khi mã nguồn bị gắn chặt vào một giao thức cụ thể. Ví dụ, một chương trình khách (client) viết cho IPv4 sẽ dùng:

```c
struct sockaddr_in servaddr;          // cấu trúc địa chỉ riêng của IPv4
servaddr.sin_family = AF_INET;
servaddr.sin_port   = htons(13);
inet_pton(AF_INET, "192.168.1.10", &servaddr.sin_addr);
sockfd = socket(AF_INET, SOCK_STREAM, 0);
```

Muốn chuyển sang IPv6, bạn phải sửa nhiều chỗ:

| IPv4          | IPv6           |
|---------------|----------------|
| `sockaddr_in` | `sockaddr_in6` |
| `AF_INET`     | `AF_INET6`     |
| `sin_port`    | `sin6_port`    |
| `sin_addr`    | `sin6_addr`    |

Mỗi lần đổi giao thức là một lần sửa mã nguồn.

**Độc lập giao thức** (protocol independence) là cách viết chương trình sao cho mã nguồn không bị gắn cứng vào một giao thức nào, chạy được với cả IPv4 lẫn IPv6 mà không cần sửa. Cách làm chuẩn là dùng hàm `getaddrinfo()`. Hàm này nhận vào tên máy (hostname) hoặc chuỗi địa chỉ, rồi tự trả về cấu trúc địa chỉ phù hợp (IPv4 hoặc IPv6) cùng với họ giao thức (domain) và kiểu socket (type) tương ứng. Chương trình chỉ việc dùng các giá trị trả về đó để gọi `socket()` và `connect()`, không cần biết bên dưới là giao thức nào.

Điều này khả thi vì bản thân giao diện lập trình socket (sockets API – tập hàm socket) được thiết kế từ đầu để hỗ trợ nhiều họ giao thức (protocol family) khác nhau. Các hàm như `bind()`, `connect()` nhận tham số kiểu chung `struct sockaddr *`, còn cấu trúc địa chỉ cụ thể thì thay đổi theo từng giao thức.

### 1.2. Mô hình OSI (OSI Model)

Mô hình OSI (Open Systems Interconnection – Kết nối các hệ thống mở) do tổ chức ISO đưa ra, chia quá trình truyền thông mạng thành 7 tầng. Mỗi tầng đảm nhận một nhiệm vụ riêng và chỉ làm việc với tầng ngay trên và ngay dưới nó.

| Tầng OSI                        | Tương ứng trong bộ giao thức Internet                   | Nằm ở đâu                     |
|---------------------------------|---------------------------------------------------------|-------------------------------|
| 7. Ứng dụng (Application)       | Ứng dụng (HTTP, FTP, chương trình của bạn…)             | Tiến trình người dùng (user process) |
| 6. Trình bày (Presentation)     | ↑ (gộp vào tầng ứng dụng)                               | ↑                             |
| 5. Phiên (Session)              | ↑ (gộp vào tầng ứng dụng)                               | ↑                             |
| 4. Vận chuyển (Transport)       | TCP, UDP                                                | Nhân hệ điều hành (kernel)    |
| 3. Mạng (Network)               | IPv4, IPv6                                              | Nhân hệ điều hành (kernel)    |
| 2. Liên kết dữ liệu (Data link) | Trình điều khiển thiết bị và phần cứng (ví dụ Ethernet) | Trình điều khiển + phần cứng  |
| 1. Vật lý (Physical)            | ↑                                                       | ↑                             |

![Sơ đồ đóng gói và mở gói dữ liệu](image.png)

#### Dữ liệu được gửi qua mạng như thế nào? Tại sao mô hình OSI cần nhiều tầng như vậy?

Sơ đồ trên cho thấy dữ liệu được đóng gói (encapsulation) và mở gói (de-encapsulation) như thế nào khi truyền qua mạng.

1. **Bước 1:** Khi thiết bị A gửi dữ liệu đến thiết bị B qua mạng bằng giao thức HTTP, dữ liệu trước tiên được thêm một phần đầu HTTP (HTTP header) ở tầng ứng dụng.
2. **Bước 2:** Tiếp theo, một phần đầu TCP hoặc UDP được thêm vào dữ liệu. Dữ liệu được đóng gói thành các phân đoạn TCP (TCP segment) ở tầng vận chuyển. Phần đầu này chứa cổng nguồn, cổng đích và số thứ tự (sequence number).
3. **Bước 3:** Các phân đoạn sau đó được đóng gói thêm phần đầu IP ở tầng mạng. Phần đầu IP chứa địa chỉ IP nguồn và địa chỉ IP đích.
4. **Bước 4:** Gói tin IP (IP datagram) được thêm phần đầu MAC ở tầng liên kết dữ liệu, chứa địa chỉ MAC nguồn và địa chỉ MAC đích.
5. **Bước 5:** Các khung (frame) đã đóng gói được chuyển xuống tầng vật lý và truyền qua mạng dưới dạng các bit nhị phân.
6. **Bước 6–10:** Khi thiết bị B nhận các bit từ mạng, nó thực hiện quá trình mở gói, tức là làm ngược lại quá trình đóng gói. Các phần đầu được gỡ bỏ lần lượt qua từng tầng, và cuối cùng thiết bị B đọc được dữ liệu.

> Mô hình mạng cần chia tầng vì mỗi tầng chỉ tập trung vào trách nhiệm riêng của mình. Mỗi tầng dựa vào phần đầu (header) để biết cách xử lý, và không cần hiểu ý nghĩa của dữ liệu đến từ tầng khác.

### 1.3. Lịch sử mạng trên BSD (BSD Networking History)

BSD (Berkeley Software Distribution) là một nhánh hệ điều hành UNIX do nhóm CSRG (Computer Systems Research Group – Nhóm nghiên cứu hệ thống máy tính) tại Đại học California, Berkeley phát triển.

Các mốc chính:

| Năm        | Phiên bản                                   | Ý nghĩa |
|------------|---------------------------------------------|---------|
| 1983       | 4.2BSD                                      | Bản phát hành đầu tiên được phổ biến rộng rãi có giao diện lập trình socket và bộ giao thức TCP/IP. Đây là nơi giao diện lập trình socket ra đời. |
| 1986–1990  | 4.3BSD (1986), 4.3BSD Tahoe (1988), 4.3BSD Reno (1990) | Tiếp tục cải tiến mã mạng. |
| 1989, 1991 | Net/1, Net/2                                | Phát hành riêng phần mã mạng dưới dạng mã nguồn tự do, không cần giấy phép UNIX của AT&T. |
| 1993–1995  | 4.4BSD (1993), 4.4BSD-Lite / Net/3 (1994), 4.4BSD-Lite2 (1995) | Các bản phát hành cuối cùng của Berkeley. |

---

## Phần 2. Địa chỉ socket và thứ tự byte

### 2.1. Cấu trúc địa chỉ socket (Socket Address Structures)

- Một giao thức tầng vận chuyển trong bộ TCP/IP cần cả địa chỉ IP lẫn số cổng (port) ở mỗi đầu để tạo kết nối. Sự kết hợp giữa một địa chỉ IP và một số cổng được gọi là **địa chỉ socket** (socket address).
- Địa chỉ socket của máy khách xác định duy nhất tiến trình khách, cũng như địa chỉ socket của máy chủ xác định duy nhất tiến trình chủ, như minh họa trong hình.

![Địa chỉ socket của máy khách và máy chủ](image-1.png)

- Để sử dụng dịch vụ của tầng vận chuyển trên Internet, ta cần một cặp địa chỉ socket: địa chỉ socket của máy khách và địa chỉ socket của máy chủ.
- Bốn thông tin này nằm trong phần đầu gói tin tầng mạng và phần đầu gói tin tầng vận chuyển. Phần đầu thứ nhất chứa các địa chỉ IP; phần đầu thứ hai chứa các số cổng.

**Vấn đề mới:** chỉ có một bộ hàm socket nhưng phải nhận được nhiều loại địa chỉ.

**Giải pháp:** mọi cấu trúc địa chỉ đều tuân theo một quy ước: trường đầu tiên luôn cho biết họ địa chỉ (family), kèm theo đó là độ dài của cấu trúc.

Nhân hệ điều hành nhận một khối byte, đọc trường đầu tiên để biết "đây là IPv4" hay "đây là IPv6", rồi mới biết cách hiểu các byte còn lại. Cấu trúc chung (`sockaddr`) chỉ là cái "khung" thể hiện quy ước này, không dùng để chứa địa chỉ thật. Đây cũng chính là điều giúp giao diện lập trình socket đạt được tính độc lập giao thức.

### 2.2. Tham số giá trị – kết quả (Value-Result Arguments)

Tiến trình và nhân hệ điều hành nằm ở hai vùng bộ nhớ tách biệt: **không gian người dùng** (user space – vùng nhớ của tiến trình) và **không gian nhân** (kernel space – vùng nhớ của nhân). Tiến trình không được đọc bộ nhớ của nhân. Vì vậy khi nhân muốn trả địa chỉ về (ví dụ "máy khách vừa kết nối đến có địa chỉ gì"), nó không thể đưa cho tiến trình một con trỏ vào bộ nhớ của nhân. Cách duy nhất: tiến trình chuẩn bị sẵn một vùng nhớ, nhân chép dữ liệu vào đó.

Từ đây sinh ra hai câu hỏi:

1. **Nhân được phép ghi bao nhiêu byte?** Nếu nhân ghi nhiều hơn vùng nhớ tiến trình cấp, dữ liệu sẽ tràn sang vùng khác (tràn bộ đệm – buffer overflow).
2. **Nhân thực sự đã ghi bao nhiêu byte?** Địa chỉ IPv4 và IPv6 có kích thước khác nhau, nên tiến trình cần biết kết quả thực tế dài bao nhiêu.

**Giải pháp:** dùng một biến độ dài cho cả hai vai trò, và tiến trình truyền địa chỉ của biến đó để nhân có thể sửa nó:

| Thời điểm   | Biến độ dài cho biết        |
|-------------|-----------------------------|
| Khi truyền vào | Kích thước vùng nhớ      |
| Khi trả về  | Số byte thực tế đã ghi      |

Cấu trúc địa chỉ socket được truyền từ nhân sang tiến trình:

![Cấu trúc địa chỉ socket truyền từ nhân sang tiến trình](image-3.png)

### 2.3. Thứ tự byte (Byte Ordering)

![Minh họa thứ tự byte](image-2.png)

#### Thứ tự byte nhỏ trước (Little Endian)

Bộ xử lý Intel x86 lưu một số nguyên 2 byte theo cách: byte ít quan trọng nhất (byte thấp) đứng trước, tiếp theo là byte quan trọng nhất (byte cao). Cách này gọi là thứ tự byte **little-endian**.

#### Thứ tự byte lớn trước (Big Endian)

Trong thứ tự byte **big-endian**, byte quan trọng nhất được lưu ở địa chỉ bộ nhớ thấp nhất, còn byte ít quan trọng nhất được lưu ở địa chỉ bộ nhớ cao nhất. Các kiến trúc PowerPC đời cũ và Motorola 68k thường dùng big-endian. Trong truyền thông mạng và lưu trữ tệp, người ta cũng dùng big-endian.

Thứ tự byte trở nên quan trọng khi dữ liệu được truyền giữa các hệ thống, hoặc được xử lý bởi các hệ thống có thứ tự byte khác nhau. Cần xử lý thứ tự byte đúng cách để dữ liệu được hiểu nhất quán trên các hệ thống khác nhau.

Thứ tự mà CPU của một máy sử dụng gọi là **thứ tự byte của máy** (host byte order). Trong phạm vi một máy thì không có vấn đề gì, vì CPU ghi và đọc theo cùng một quy ước.

#### Vấn đề thật sự xuất hiện khi truyền qua mạng

Mạng chỉ truyền một chuỗi byte liên tiếp, không mang thông tin "byte này là byte cao hay byte thấp". Nếu máy gửi là little-endian gửi `90 1F`, máy nhận là big-endian sẽ đọc thành `0x901F` = 36895. Cùng một chuỗi byte nhưng hai bên hiểu thành hai số khác nhau.

#### Giải pháp

Các giao thức mạng thống nhất một quy ước chung: mọi trường số trong phần đầu gói tin đều dùng big-endian. Quy ước này gọi là **thứ tự byte mạng** (network byte order).

Từ đó sinh ra các **hàm chuyển đổi thứ tự byte** (byte ordering functions): chúng chuyển số từ thứ tự byte của máy sang thứ tự byte mạng khi gửi, và ngược lại khi nhận. Bản chất của các hàm này:

- Trên máy little-endian: chúng đảo thứ tự byte.
- Trên máy big-endian: chúng không làm gì cả, vì thứ tự của máy đã trùng với thứ tự mạng.

Nhờ vậy cùng một đoạn mã chạy đúng trên mọi loại CPU, lập trình viên không cần biết máy mình thuộc loại nào. Đây là **tính khả chuyển** (portability), tức mã nguồn chạy được trên nhiều nền tảng mà không phải sửa.

---

## Phần 3. Các hàm `inet_aton`, `inet_pton`, `inet_ntop`

Một địa chỉ IP tồn tại ở hai dạng:

- **Dạng văn bản** (presentation / text form): là chuỗi ký tự mà con người đọc và gõ được, ví dụ `"192.168.1.10"` (IPv4, gọi là dạng thập phân có dấu chấm – dotted-decimal, tức 4 số thập phân ngăn cách bởi dấu chấm) hoặc `"fe80::1"` (IPv6, dạng thập lục phân ngăn cách bởi dấu hai chấm).
- **Dạng nhị phân** (numeric / binary form): là giá trị số mà nhân hệ điều hành và phần đầu gói tin IP sử dụng. Với IPv4 là một số 32 bit, với IPv6 là 128 bit, theo thứ tự byte mạng. Đây là dạng được lưu trong trường địa chỉ của cấu trúc địa chỉ socket.

Hai dạng này hoàn toàn khác nhau trong bộ nhớ. Người dùng nhập chuỗi (từ tệp cấu hình, dòng lệnh), còn cấu trúc địa chỉ cần số nhị phân. Khi nhân trả về địa chỉ máy khách, bạn nhận được số nhị phân nhưng muốn in ra nhật ký (log) thì cần chuỗi. Vì vậy cần các hàm chuyển đổi qua lại giữa hai dạng.

### 3.1. `inet_aton` – chuỗi → nhị phân (chỉ IPv4)

```c
int inet_aton(const char *str, struct in_addr *addr);
```

- Chuyển chuỗi `str` thành số nhị phân và ghi vào `addr`.
- Trả về `1` nếu chuỗi hợp lệ, `0` nếu không hợp lệ.
- Hàm này khá "dễ dãi": ngoài dạng 4 phần chuẩn, nó còn chấp nhận các dạng rút gọn và số thập lục phân/bát phân (ví dụ `"10.1"` hay `"0x7f.1"`), điều này đôi khi gây bất ngờ.

### 3.2. `inet_pton` – chuỗi → nhị phân (cả IPv4 và IPv6)

```c
int inet_pton(int family, const char *str, void *addr);
```

| Tham số  | Ý nghĩa |
|----------|---------|
| `family` | `AF_INET` hoặc `AF_INET6`, cho hàm biết cần hiểu chuỗi theo kiểu nào |
| `str`    | Chuỗi địa chỉ |
| `addr`   | Nơi ghi kết quả. Với IPv4 là con trỏ tới `struct in_addr`, với IPv6 là con trỏ tới `struct in6_addr`. Kiểu `void *` cho phép nhận cả hai |

Giá trị trả về có ba trường hợp, và đây là điểm cải tiến so với `inet_addr`:

| Giá trị | Ý nghĩa |
|---------|---------|
| `1`     | Thành công |
| `0`     | Chuỗi không đúng định dạng của họ địa chỉ đã chọn |
| `-1`    | Lỗi, ví dụ họ địa chỉ không được hỗ trợ (`errno = EAFNOSUPPORT`) |

> Với `AF_INET`, hàm này nghiêm ngặt: chỉ chấp nhận dạng 4 số thập phân chuẩn, không nhận dạng rút gọn hay thập lục phân như `inet_aton`.

### 3.3. `inet_ntop` – nhị phân → chuỗi

```c
const char *inet_ntop(int family, const void *addr, char *str, socklen_t size);
```

| Tham số | Ý nghĩa |
|---------|---------|
| `addr`  | Con trỏ tới địa chỉ nhị phân (`in_addr` hoặc `in6_addr`) |
| `str`   | Vùng nhớ do bạn cấp để hàm ghi chuỗi vào |
| `size`  | Kích thước vùng nhớ đó, để hàm không ghi tràn. Nếu vùng nhớ quá nhỏ, hàm trả về `NULL` và đặt `errno = ENOSPC` |

**Trả về:** con trỏ `str` nếu thành công, `NULL` nếu lỗi.

---

## Phần 4. Các hàm socket cơ bản

Các hàm được đề cập: `socket`, `connect`, `bind`, `listen`, `accept`.

Trình tự gọi hàm của một cặp client/server TCP như sau:

```text
        SERVER                                   CLIENT
   socket()                                 socket()
      │                                        │
   bind()      ← gắn IP:port "nổi tiếng"       │  (thường không bind, nhân tự chọn cổng tạm)
      │                                        │
   listen()    ← CLOSED → LISTEN               │
      │                                        │
   accept()    ← chặn, chờ kết nối       connect()  ← gửi SYN, bắt tay 3 bước
      │  ◄──────────── bắt tay 3 bước ─────────┤
      │  trả về socket MỚI (connfd)            │  trả về 0 khi đã ESTABLISHED
   read/write  ◄────────── dữ liệu ──────────► write/read
   close()                                  close()
```

### 4.1. `socket()`: tạo một điểm cuối truyền thông

```c
#include <sys/socket.h>

int socket(int family, int type, int protocol);
```

| Tham số    | Ý nghĩa                 | Giá trị thường dùng |
|------------|-------------------------|---------------------|
| `family`   | Họ giao thức (domain)   | `AF_INET` (IPv4), `AF_INET6` (IPv6), `AF_LOCAL`/`AF_UNIX` (socket nội bộ trong máy), `AF_ROUTE`, `AF_KEY` |
| `type`     | Kiểu socket             | `SOCK_STREAM` (luồng byte), `SOCK_DGRAM` (gói tin), `SOCK_SEQPACKET`, `SOCK_RAW` |
| `protocol` | Giao thức cụ thể        | `0` để nhân tự chọn giao thức mặc định cho cặp family/type, hoặc `IPPROTO_TCP`, `IPPROTO_UDP`, `IPPROTO_SCTP` |

Các tổ hợp hợp lệ thường gặp:

|               | `AF_INET` | `AF_INET6` |
|---------------|-----------|------------|
| `SOCK_STREAM` | TCP       | TCP        |
| `SOCK_DGRAM`  | UDP       | UDP        |
| `SOCK_RAW`    | IPv4 thô  | IPv6 thô   |

**Giá trị trả về:** một số nguyên không âm gọi là bộ mô tả socket (socket descriptor, `sockfd`), giống như bộ mô tả tệp (file descriptor). Trả về `-1` nếu lỗi và đặt `errno`.

**Bản chất:**

- `socket()` chỉ tạo ra một cấu trúc dữ liệu trong nhân và cho biết socket sẽ dùng giao thức nào. Socket lúc này **chưa có địa chỉ** (chưa có IP và cổng cục bộ), cũng **chưa kết nối** tới ai.
- Vì socket cũng là một descriptor nên sau này có thể dùng `read()`, `write()`, `close()` như với tệp.
- Nếu kết hợp với `getaddrinfo()` (Phần 1), ta truyền thẳng `ai_family`, `ai_socktype`, `ai_protocol` vào `socket()`, nên mã nguồn không phụ thuộc vào giao thức.

### 4.2. `connect()`: client thiết lập kết nối tới server

```c
int connect(int sockfd, const struct sockaddr *servaddr, socklen_t addrlen);
```

| Tham số    | Ý nghĩa |
|------------|---------|
| `sockfd`   | Socket do `socket()` trả về |
| `servaddr` | Con trỏ tới cấu trúc địa chỉ socket của server (IP + cổng). Kiểu chung `struct sockaddr *` nên phải ép kiểu: `(struct sockaddr *) &servaddr` |
| `addrlen`  | Kích thước của cấu trúc đó, ví dụ `sizeof(servaddr)`. Đây là tham số truyền giá trị (từ tiến trình vào nhân), **không phải** tham số giá trị–kết quả |

**Trả về:** `0` nếu thành công, `-1` nếu lỗi.

**Bản chất với TCP:** `connect()` kích hoạt bắt tay 3 bước (three-way handshake) và chỉ trả về khi kết nối đã thiết lập (`ESTABLISHED`) hoặc khi có lỗi.

- Client không cần gọi `bind()` trước. Nếu chưa bind, nhân sẽ tự chọn IP nguồn (theo tuyến đường ra) và một cổng tạm (ephemeral port).
- Trạng thái TCP chuyển từ `CLOSED` sang `SYN_SENT`, rồi sang `ESTABLISHED` khi nhận được SYN+ACK.

Ba lỗi kinh điển:

| Tình huống | Lỗi | Giải thích |
|------------|-----|------------|
| Không nhận được phản hồi cho SYN (gửi lại nhiều lần, tổng khoảng 75 giây) | `ETIMEDOUT` | Server tắt máy, IP không tồn tại trong mạng LAN… |
| Server trả về RST | `ECONNREFUSED` | **Hard error** (lỗi cứng): máy server có tồn tại nhưng không có tiến trình nào lắng nghe ở cổng đó |
| Router trung gian trả về ICMP "destination unreachable" | `EHOSTUNREACH` / `ENETUNREACH` | **Soft error** (lỗi mềm): nhân vẫn thử lại, sau khoảng 75 giây mới báo lỗi |

> **Lưu ý quan trọng:** nếu `connect()` thất bại, socket đó không dùng lại được nữa. Phải `close()` rồi gọi `socket()` lại. Đây là lý do vòng lặp thử từng địa chỉ trả về từ `getaddrinfo()` luôn tạo socket mới cho mỗi lần thử.

### 4.3. `bind()`: gắn địa chỉ cục bộ cho socket

```c
int bind(int sockfd, const struct sockaddr *myaddr, socklen_t addrlen);
```

| Tham số   | Ý nghĩa |
|-----------|---------|
| `myaddr`  | Địa chỉ cục bộ (IP + cổng của chính máy mình) muốn gắn vào socket |
| `addrlen` | Kích thước cấu trúc (tham số truyền giá trị) |

**Trả về:** `0` nếu thành công, `-1` nếu lỗi.

**Tại sao cần?** Server phải có một cổng cố định, được biết trước (well-known port, ví dụ 80 cho HTTP, 13 cho daytime) để client biết mà kết nối tới. Client thường không cần, vì chỉ dùng cổng tạm.

Bốn cách kết hợp IP và cổng khi bind:

| Địa chỉ IP | Cổng | Kết quả |
|------------|------|---------|
| Wildcard (`INADDR_ANY` / `in6addr_any`) | `0` | Nhân chọn cả IP lẫn cổng |
| Wildcard | khác `0` | Nhân chọn IP, tiến trình chọn cổng. **Đây là cách phổ biến nhất ở server** |
| IP cụ thể | `0` | Tiến trình chọn IP, nhân chọn cổng |
| IP cụ thể | khác `0` | Tiến trình chọn cả hai |

- Địa chỉ wildcard (`INADDR_ANY`) có nghĩa là "nhận kết nối đến bất kỳ IP nào của máy này". Máy có nhiều card mạng thì server nhận kết nối trên tất cả card đó.
- Nếu IP cụ thể thì server chỉ nhận kết nối gửi tới đúng IP đó.
- Nếu để nhân chọn cổng (cổng = 0), `bind()` không trả về cổng đã chọn (vì `myaddr` là `const`). Muốn biết cổng đó phải gọi `getsockname()`.

Ví dụ điển hình:

```c
struct sockaddr_in servaddr;
memset(&servaddr, 0, sizeof(servaddr));
servaddr.sin_family      = AF_INET;
servaddr.sin_addr.s_addr = htonl(INADDR_ANY);   // chuyển sang thứ tự byte mạng (Phần 2)
servaddr.sin_port        = htons(13);
bind(listenfd, (struct sockaddr *) &servaddr, sizeof(servaddr));
```

> **Lỗi hay gặp:** `EADDRINUSE` ("Address already in use"). Thường xảy ra khi khởi động lại server ngay sau khi tắt, lúc kết nối cũ vẫn đang ở trạng thái `TIME_WAIT`. Cách khắc phục là đặt tùy chọn `SO_REUSEADDR` bằng `setsockopt()` trước khi gọi `bind()`. Ngoài ra, bind cổng < 1024 trên UNIX cần quyền root (`EACCES`).

### 4.4. `listen()`: chuyển socket sang chế độ lắng nghe

```c
int listen(int sockfd, int backlog);
```

**Trả về:** `0` nếu thành công, `-1` nếu lỗi. Chỉ server gọi hàm này.

Hàm làm hai việc:

1. **Biến socket chủ động thành socket bị động.** Mặc định, socket tạo bởi `socket()` được coi là chủ động (active), tức là socket client sẽ gọi `connect()`. `listen()` báo cho nhân biết socket này dùng để chấp nhận kết nối đến. Trạng thái TCP chuyển từ `CLOSED` sang `LISTEN`.
2. **Quy định `backlog`:** số lượng tối đa kết nối mà nhân xếp hàng chờ cho socket này.

Hai hàng đợi mà nhân duy trì cho socket lắng nghe:

```text
 Client gửi SYN
      │
      ▼
┌────────────────────────────────┐   Nhân gửi SYN+ACK, chờ ACK
│ Hàng đợi kết nối CHƯA HOÀN TẤT │   (trạng thái SYN_RCVD)
│ (incomplete connection queue)  │
└────────────────────────────────┘
      │ nhận ACK (bắt tay 3 bước xong)
      ▼
┌────────────────────────────────┐   Trạng thái ESTABLISHED,
│ Hàng đợi kết nối ĐÃ HOÀN TẤT   │   chờ tiến trình gọi accept()
│ (completed connection queue)   │
└────────────────────────────────┘
      │ accept()
      ▼
   Tiến trình server nhận socket mới
```

- **Điểm quan trọng:** bắt tay 3 bước do nhân tự làm, không phụ thuộc vào việc server có đang gọi `accept()` hay không. `accept()` chỉ lấy ra kết nối đã hoàn tất.
- `backlog` (trên Linux hiện nay) là giới hạn của hàng đợi đã hoàn tất. Giá trị bị chặn bởi `/proc/sys/net/core/somaxconn`. Có thể dùng hằng `SOMAXCONN`.

![Hai hàng đợi của socket lắng nghe](image-4.png)

- Khi hàng đợi đầy mà có SYN mới đến, TCP **bỏ qua** SYN đó (không gửi RST). Client sẽ tự gửi lại SYN và có cơ hội được nhận sau. Nếu gửi RST, client sẽ hiểu nhầm là "không có server" (`ECONNREFUSED`).
- Không nên đặt `backlog = 0`, vì mỗi hệ thống hiểu giá trị này khác nhau.

### 4.5. `accept()`: lấy một kết nối đã hoàn tất ra khỏi hàng đợi

```c
int accept(int sockfd, struct sockaddr *cliaddr, socklen_t *addrlen);
```

| Tham số   | Ý nghĩa |
|-----------|---------|
| `sockfd`  | Socket lắng nghe (listening socket) đã qua `bind()` và `listen()` |
| `cliaddr` | Vùng nhớ do tiến trình cấp, nhân sẽ ghi địa chỉ của client vào đây |
| `addrlen` | Tham số giá trị–kết quả (value-result argument, xem [Phần 2.2](#22-tham-số-giá-trị--kết-quả-value-result-arguments)) |

Với `addrlen`:

- Khi truyền vào: `*addrlen` = kích thước vùng nhớ `cliaddr`.
- Khi trả về: `*addrlen` = số byte nhân thực sự đã ghi.
- Nếu không cần biết địa chỉ client, có thể truyền `NULL` cho cả `cliaddr` và `addrlen`.

**Trả về:** một bộ mô tả socket **MỚI**, gọi là socket đã kết nối (connected socket, `connfd`). Trả về `-1` nếu lỗi.

Phân biệt hai loại socket (điểm dễ nhầm nhất):

|           | Socket lắng nghe (`listenfd`)          | Socket đã kết nối (`connfd`) |
|-----------|----------------------------------------|------------------------------|
| Tạo bởi   | `socket()` + `bind()` + `listen()`     | `accept()` |
| Số lượng  | Một cho cả vòng đời server             | Một cho mỗi client |
| Dùng để   | Chỉ để nhận kết nối mới                | `read()`/`write()` dữ liệu với đúng một client |
| Đóng khi  | Server tắt                             | Phục vụ xong client đó |

- **Hành vi chặn (blocking):** nếu hàng đợi đã hoàn tất đang rỗng, tiến trình bị ngủ (sleep) cho tới khi có kết nối mới.
- Mỗi kết nối TCP được xác định bởi bộ 4 giá trị (IP client, cổng client, IP server, cổng server). Vì vậy nhiều `connfd` có thể dùng chung cổng server (ví dụ 13) mà không bị lẫn nhau.

---

## Phần 5. Server đồng thời (Concurrent Servers)

**Vấn đề:** server lặp (iterative server) phục vụ từng client một. Nếu một client giữ kết nối lâu, các client khác phải nằm chờ trong hàng đợi accept.

**Giải pháp:** sau mỗi lần `accept()`, gọi `fork()` để tạo một tiến trình con phục vụ riêng client đó. Tiến trình cha quay lại `accept()` ngay.

### 5.1. `fork()`

```c
pid_t fork(void);
```

Hàm được gọi **một lần** nhưng trả về **hai lần**:

| Trả về ở      | Giá trị |
|---------------|---------|
| Tiến trình cha | PID của con (`> 0`) |
| Tiến trình con | `0` |
| Lỗi           | `-1` |

Con là bản sao của cha và dùng chung các socket đã mở trước `fork()`: cả hai cùng trỏ tới một socket trong nhân.

### 5.2. Khung chương trình

```c
for (;;) {
    connfd = accept(listenfd, ...);
    if (fork() == 0) {          // con
        close(listenfd);
        doit(connfd);
        close(connfd);
        exit(0);
    }
    close(connfd);              // cha
}
```

### 5.3. Tại sao phải `close()` ở cả hai phía?

Mỗi socket có một **bộ đếm tham chiếu** (reference count). `close()` chỉ giảm bộ đếm đi 1; nhân chỉ thật sự đóng socket (gửi FIN) khi bộ đếm về 0. Sau `fork()`, cả `listenfd` và `connfd` đều có bộ đếm bằng 2.

| Quên đóng | Hậu quả |
|-----------|---------|
| Cha không `close(connfd)` | Rò rỉ descriptor (sau một thời gian `accept()` báo `EMFILE`), và client không bao giờ nhận được FIN |
| Con không `close(listenfd)` | Con giữ cổng lắng nghe không cần thiết |

### 5.4. Ưu và nhược điểm

| Ưu điểm | Nhược điểm |
|---------|------------|
| Mã đơn giản, mỗi con chỉ lo một client | `fork()` tốn chi phí |
| Một con bị lỗi không ảnh hưởng tới các client khác | Mỗi kết nối tốn một tiến trình, khó mở rộng lên số lượng lớn |

---

## Phần 6. Các hàm `close`, `getsockname`, `getpeername`

### 6.1. `close()`: đóng socket

```c
#include <unistd.h>

int close(int sockfd);
```

**Trả về:** `0` nếu thành công, `-1` nếu lỗi.

**Bản chất với TCP:**

- `close()` đánh dấu descriptor là đã đóng và trả về ngay. Từ đó tiến trình không được dùng descriptor này để `read()`/`write()` nữa.
- Nhân vẫn cố gửi hết dữ liệu còn trong bộ đệm gửi, sau đó mới gửi FIN để bắt đầu quá trình kết thúc kết nối TCP bình thường.
- `close()` chỉ giảm bộ đếm tham chiếu đi 1. Nhân chỉ gửi FIN khi bộ đếm về 0 (xem [Phần 5.3](#53-tại-sao-phải-close-ở-cả-hai-phía)).

Khi cần hành vi khác:

| Cần | Dùng |
|-----|------|
| Gửi FIN ngay, bất kể bộ đếm tham chiếu | `shutdown(sockfd, SHUT_WR)` |
| Đóng một chiều: ngừng gửi nhưng vẫn đọc được | `shutdown(sockfd, SHUT_WR)` |
| `close()` chặn cho tới khi dữ liệu được gửi xong, hoặc hủy kết nối bằng RST | Tùy chọn `SO_LINGER` |

### 6.2. `getsockname()` và `getpeername()`

```c
#include <sys/socket.h>

int getsockname(int sockfd, struct sockaddr *localaddr, socklen_t *addrlen);
int getpeername(int sockfd, struct sockaddr *peeraddr,  socklen_t *addrlen);
```

| Hàm             | Trả về địa chỉ nào |
|-----------------|--------------------|
| `getsockname()` | Địa chỉ cục bộ (IP + cổng của chính mình) gắn với socket |
| `getpeername()` | Địa chỉ đầu bên kia (IP + cổng của máy đối diện) |

- `addrlen` là tham số giá trị–kết quả, giống `accept()`. Khi truyền vào, nó là kích thước vùng nhớ; khi trả về, nó là số byte nhân đã ghi.
- **Trả về:** `0` nếu thành công, `-1` nếu lỗi. `getpeername()` trên socket chưa kết nối báo lỗi `ENOTCONN`.

Khi nào cần dùng:

| Tình huống | Hàm | Lý do |
|------------|-----|-------|
| Client gọi `connect()` mà không `bind()` | `getsockname()` | Biết IP và cổng tạm mà nhân đã chọn |
| `bind()` với cổng 0 | `getsockname()` | Biết nhân đã chọn cổng nào (`bind()` không trả về giá trị này) |
| Server bind địa chỉ wildcard (`INADDR_ANY`) | `getsockname(connfd, ...)` sau `accept()` | Biết client đã kết nối tới IP nào của máy (khi máy có nhiều card mạng) |
| Chưa biết họ địa chỉ của một socket | `getsockname()` với `struct sockaddr_storage` | Đọc trường `ss_family` để biết đó là `AF_INET` hay `AF_INET6` |
| Tiến trình con được tạo bằng `fork()` rồi `exec()` sang chương trình khác (ví dụ `inetd`) | `getpeername()` | Sau `exec()`, chương trình mới chỉ còn descriptor, đã mất biến `cliaddr` mà `accept()` điền. Chỉ còn cách hỏi nhân địa chỉ client |

Ví dụ: in cổng tạm của client sau khi kết nối:

```c
struct sockaddr_in local;
socklen_t len = sizeof(local);
getsockname(sockfd, (struct sockaddr *) &local, &len);
printf("Cổng cục bộ: %d\n", ntohs(local.sin_port));
```