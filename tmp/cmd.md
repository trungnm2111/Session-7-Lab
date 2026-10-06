gdb -q --args ./ringd -v

(gdb) set detach-on-fork off      # giữ cả 6 process (supervisor, 4 worker, collector) dưới gdb
(gdb) set follow-fork-mode parent
(gdb) set schedule-multiple on    # để tất cả cùng chạy, không chỉ process đang được chọn
(gdb) run


theo dõi worker 
(gdb) bt                                         # (a) đang dừng ở dòng nào?
(gdb) frame 1                                    # chuyển vào process_request
(gdb) p/x hdr                                    # (b) header mà worker "đọc" được
(gdb) p n                                        # (c) số byte payload worker dùng
(gdb) p shm->head
(gdb) p shm->ring[(shm->head-1) % 64]            # (d) slot worker vừa ghi vào ring
(gdb) p shm->lock.__data.__owner                 # (e) ai đang giữ lock?
(gdb) info inferiors 


theo dõi collector
(gdb) continue
gdb sẽ dừng lần nữa với Thread 6.1 "ringd" received signal SIGSEGV. Inferior số 6 chính là collector.


(gdb) bt                                   # (a)
(gdb) frame 1                              # vào consume()
(gdb) p s->len                             # (b)
(gdb) p s->worker_pid                      # (c)
(gdb) p sizeof(buf)                        # (d)
(gdb) p &buf                               # (e)
(gdb) info proc mappings                   # (f) tìm dòng [stack], ghi lại End Addr
(gdb) p (char*)&buf + s->len               # (g) memcpy muốn ghi tới tận đâu
(gdb) frame 2                              # vào collector_main()
(gdb) p shm->tail                          # (h)
(gdb) p shm->head

sudo sysctl kernel.yama.ptrace_scope=0  nới quyền sudo với gdb -p pid
