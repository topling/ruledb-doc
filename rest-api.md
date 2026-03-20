# <a name="rest-api"></a> RESTful 服务
RuleDB 实现了一个简单的 HTTP REST API Service，使用的是 civetweb server。支持多个 Database。输入输出均使用 json。

> [**下载 90 天试用版(包括 SDK)**](https://topling-tools.oss-cn-qingdao.aliyuncs.com/topling-ruledb-trial90-Linux-x86_64.tgz)，满意可 [购买正式版](mailto:contact@topling.cn)

## <a name="一启动-ruledb_rest_http"></a>（一）启动 ruledb_rest_http
启动前请正确设置 `PATH` 与 `LD_LIBRARY_PATH`。命令行参数：

* `-Dname=value` 或
* `--name=value`

本服务自身的选项参数：

参数名    | 参数类型 | 默认值 |说明
---------|---------|-------|----------------------------------------------------------------
dbroot   | string  | 无    | RuleDB 多个 Database 的根目录，每个子目录是一个 RuleDB Database
tmpdir   | string  | /tmp  | 临时目录，用做在线编译，该目录应当和 dbroot **位于同一个文件系统**，从而编译成功后可以原子性地将编译输出目录移动(安装)到 dbroot
loglevel | int     | 0     | 日志级别，日志直接输出到 stderr，日志级别是数字 0 ~ 3，越大越详细

其它 name 与 value 透传至 [civetweb 的选项](https://github.com/civetweb/civetweb/blob/master/docs/UserManual.md) 。

本服务启动时自动扫描所有 dbroot 下的所有 Database 目录，在线编译产生的新 Database 移动(安装)到 dbroot 之后也会自动加载。

## （二）REST API

### 2.1 /_health
HTTP Method: GET

健康检查，返回 `{"status": "OK"}`

### 2.2 /_list?pretty={bool}
HTTP Method: GET

列出所有 Database，示例：

```bash
curl http://127.0.0.1:1082/_list?pretty=1
```

```json
{
  "status": "OK",
  "databases": {
    "test1" : { "seqversion": 2, "total_rules": 79 },
    "test2" : { "seqversion": 5, "total_rules": 81323 }
  }
}
```

### 2.3 /_compile?argname1=argvalue1...
HTTP Method: POST

POST body 是规则源码，不是 JSON。

参数名  |参数类型|说明
-------|-------|-------------------------------------------------------------
dbname |string |指定 Database 的名字，只能包含字母数字和`_`,`-`,`.`，首字符只能为字母
overwrite|bool|是否覆盖替换已存在的Database
F      |string |定义字段，可指定多次定义多个字段，同[编译器命令行参数](README.md#ruledb-编译器)
i      |string |定义索引，可指定多次定义多个索引，同[编译器命令行参数](README.md#ruledb-编译器)
S[大写] |bool  |默认 true；表示是否生成(详尽的) vm 汇编代码，用来辅助调试，提高可观测性、可解释性
q	     |无参数  |不打印进度及不重要的警告信息等

示例：
```bash
curl --data-binary @file \
  'http://127.0.0.1:1082/_compile?dbname=db_123a&F=brandname&F=osname&overwrite=true'
```

file 的内容不是 JSON，是多行文本文件，每行内容为 `规则表达式` `\t` `业务id`，
不同于[直接使用 SDK](README.md#2-关联业务数据)，这里 `业务id` 只能是一个32位整数，
非法内容会被当做整数 0。

业务id 是该规则关联的业务逻辑的标识，例如 1 代表涉黄，2 代表涉政，3 代表涉医，4 代表广告软文……

规则可以很少，也可以很多(百万条)，可以映射到很具体业务对象（例如具体的哪个广告主）。

单个规则可以很小只有几个关键字（例如广告定向），也可以很大包括几十万个关键字的几万个bool not/and/or/near(例如海量敏感词)。

该服务使用了 inotify，编译成功后该服务会自动加载新的 Database 或 hotswap 原有的 Database。

返回结果：
```json
{
  "status": "OK",
  "dbname": "word_rule_with_id_by_web",
  "seqversion": 1,
  "beg_ts": "2026-03-15 12:12:44",
  "end_ts": "2026-03-15 12:12:47",
  "duration_sec": 2.8641564020000003,
  "stdout": "很多内容，略",
  "stderr": "很多内容，略"
}
```
如果是编译新 Database 则 seqversion=0，否则是现有 Database 的 seqversion，_compile 触发的随后的自动加载/热更新会设置/增加 seqversion。

**注意：**

1. 使用 curl 命令时必须用 `--data-binary`，不能用 `-d/--data`，不然会丢失换行符导致编译错误；
1. 使用手动编译代替该 _compile 命令时：
   * 传给 [编译器 rule_db_build.sh](README.md#ruledb-编译器) 的输出目录应当和 dbroot 在同一个文件系统中，编译成功后移动到 dbroot 中；
   * 如果直接将编译输出目录指定为 dbroot 的子目录，务必保证输出目录是**新目录**，编译成功后需要手动执行 _hotswap REST（虽然执行 _match 时会自动加载 Database，但会导致在 _match 中加载 DB，从而导致 _match 产生高延迟）。

### 2.4 /{Database}/_match?argname1=argvalue1...

#### Request (输入参数)
HTTP Method: POST

参数名         |参数类型| 默认值  |说明
--------------|-------|--------|-----------------------------------------------------------
pretty        |bool   |false   |json pretty 格式化打印
withpos       |int    |0       |0: 不要匹配位置<br>1: 需要匹配位置 <br>2: 带上 match_type 和 match_id
`treat_unknown` `_fields_` `as_content`|bool|true|让未知字段按 content 字段进行匹配
`regex_start` `_at_word`|bool|false |正则表达式匹配时从 word 边界开始，一般无需设置

POST body 为 JSON [详情参考](README.md#43-json-字符串作为-doc)

#### Response(输出结果)
输出是 JSON 对象，示例：

```bash
curl --data-binary @file 'http://127.0.0.1:1082/db_123a/_match?pretty=1&withpos=2'
```

```json
{
  "status": "OK",
  "matched": [
    {
      "id": 703, "rule_id": 941,
      "matchpos": {
        ".title": [
          {"match_type": "exact", "match_id": 4803, "pos": 6, "len": 6 }
        ],
        "content": [
          {"match_type": "exact", "match_id": 94357, "pos": 369, "len": 12},
          {"match_type": "exact", "match_id": 15182, "pos": 782, "len": 6 }
        ],
        "__usec_pure__": 18.357
      }
    }
  ],
  "candidates": 35,
  "total_rules": 89723,
  "seqversion": 3,
  "read_usec": 8.831,
  "init_usec": 19.366,
  "match_usec": 117.519,
  "matchpos_usec_all": 17.062,
  "matchpos_usec_pure": 8.357
}
```
`id` 是业务方的 id，`rule_id` 是 RuleDB 编译器时分配的内部 id 。

`matchpos` 中重要的是 `pos` 和 `len`，均以字节为单位(例如len=6一般是2个汉字)。url 参数中未指定 withpos 或指定为 0 时，输出中无 matchpos。

`candidates` 是召回产生的候选规则数量，业务一般不用关心 `candidates`，`matched` 是对候选集进行验证之后得到的实际匹配的规则，是业务所需，此处仅有一条，实际可以有多条。

`total_rules` 是数据库中规则总数，业务一般也不用关心。

`seqversion` 是该 Database 在线加载/更新的 seq，从 1 开始。

`usec` 相关字段是耗时，pure 指 SDK 耗时，其它耗时包括生成 json 的耗时。

### 2.5 /{Database}/_hotswap

HTTP Method: POST

热更新，重新加载该 Database，会导致 seqversion 增加 1。一般仅用于：
1. 手动编译直接指定输出目录为 dbroot 的子目录；
2. inotify 无法正常工作时（例如通过 NFS 加载的 Database）。

使用 _compile 在线编译会自动热更新，无需调用 _hotswap。

### 2.6 /_stop

**HTTP Method: POST**

停止服务，**注意:** 这是个危险操作！

服务终止后需手动重启进程才能恢复。

### 2.7 /_buildinfo
HTTP Method: GET

参数名         |参数类型| 默认值  |说明
--------------|-------|--------|-----------------------------------------------------------
pretty        |bool   |false   |json pretty 格式化打印
verbose       |bool   |false   |输出详细信息

示例：

```bash
curl "http://127.0.0.1:10822/_buildinfo?pretty=1&verbose=1"
```

返回：

```json
{
  "status": "OK",
  "githash": "eb3ea22fff55413087a1dc69d38d37c133818cc1",
  "buildinfo": "省略...\n\ncpu_flag: -march=haswell -mbmi -mbmi2\n"
}
```

## （三） C++ API

选择使用以下两个函数，用户程序也可以将本 RESTful 服务嵌入应用程序自身从而避免额外开启 RESTful 进程。

```c++
bool ruledb_run_rest_service(const std::vector<std::string>& options);
int  ruledb_rest_main(int argc, char* argv[]);
```

### 3.1 ruledb_run_rest_service
`options` 参数遵循 civetweb 惯例：

* `options[2*i + 0]` 是第 i 个选项 name
* `options[2*i + 1]` 是第 i 个选项 value

参数说明如 [（一）](rest-api.md#一启动-ruledb_rest_http) 所述。

### 3.2 ruledb_rest_main
解析符合 main 函数约定的参数并转调 `ruledb_run_rest_service`。

ruledb package 的 samples 目录下的的 `ruledb_rest_http` 程序是 ruledb_rest_main 的包装程序：
```c++
#include <rule_db.h>
int main(int argc, char* argv[]) {
    return topling::ruledb_rest_main(argc, argv);
}
```
