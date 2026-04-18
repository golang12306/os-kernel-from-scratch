# gethostbyname 演示

## 文章核心内容

- 老式 DNS 解析函数（只支持 IPv4）
- 返回静态 struct hostent
- 不是线程安全
- 已被 getaddrinfo 取代
