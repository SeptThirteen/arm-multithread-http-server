# ARM Multi-thread Concurrent HTTP Server

一个基于 C + pthread + SQLite 的多线程并发 HTTP 服务器课程项目实现。

## 已实现功能

1. HTTP 请求解析（GET/POST、请求头、Content-Length）。
2. 静态资源处理（HTML/CSS/JS/图片）。
3. 用户注册接口（写入 SQLite）。
4. 用户登录接口（从 SQLite 验证）。
5. 线程池并发处理连接。
6. SIGINT (Ctrl+C) 优雅退出，关闭监听并回收资源。

## 目录结构

- include/: 头文件
- src/: C 源文件
- www/: 静态页面
- users.db: 运行后自动创建

## Linux/ARM 编译

```bash
make
./server 8080 8
```

参数：

- 第一个参数：端口（默认 8080）
- 第二个参数：线程数（默认 8）

## 交叉编译示例（ARM）

```bash
make CC=arm-linux-gnueabihf-gcc
```

如果 SQLite 在自定义路径：

```bash
make CC=arm-linux-gnueabihf-gcc CFLAGS="-O2 -Wall -Wextra -std=c11 -pthread -Iinclude -I/path/to/sqlite/include" LDLIBS="-L/path/to/sqlite/lib -lsqlite3"
```

## 测试

```bash
curl -v http://127.0.0.1:8080/
curl -v -X POST http://127.0.0.1:8080/api/register -d "username=alice&password=123456"
curl -v -X POST http://127.0.0.1:8080/api/login -d "username=alice&password=123456"
```
