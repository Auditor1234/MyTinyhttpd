# Tinyhttpd的复现

## 环境和配置
- ubuntu 22.04 LTS
- gcc 11.4.0
- perl v5.34.0
- perl-cgi

    1.执行`perl -MCPAN -e shell`

    2.执行`install CGI.pm`

## 使用方法
1. 终端输入：`gcc httpd.c -o httpd`
2. 终端输入：`./httpd`
3. 浏览器输入：`localhost:4000/`

原项目链接为：https://github.com/EZLippi/Tinyhttpd