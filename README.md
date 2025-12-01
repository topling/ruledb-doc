# 简介
Topling 规则数据库的功能是：将 $$\color{#DC143C}{预定义规则}$$ 作为数据库，将 $$\color{#DC143C}{输入文档}$$ 作为查询条件，搜索预定义的规则。

应用场景包括：敏感词检测、广告定向、数据分类、事件分流、自动打标签等，比现存其它方案
(例如drools/ElasticSearch percolator等)性能高至少一个数量级，规则越多，优势越突出，支持
百万条以上的规则，每条规则的表达式可以任意复杂。表达式的原生数据类型是字符串，也能
高效地支持 [数值](integer-fields.md) 与 [geo空间数据](geo-fields.md)，以及不限宽度的[整数(范围)](bigint.md)。

Topling 规则数据库是商业软件，未开源也未开放下载。开源的仅有文档。
## 规则表达式

二元操作符 | 操作符别名 | 优先级
------|-------------|-------
near  |  无         |  高
and   | `&`         |  中
not   | `&!` , `-`  |  中(二元)
or    | `\|`        |  低

所有二元操作符都是左结合的，not 既是一元操作符又是二元操作符，作为一元操作符时优先级最高。加括号 (...) 可以改变优先级。

例子            | 含义
---------------|----------------------
`中国 and 人民` | 既包含`中国`又包含`人民`
`中国 or 人民` | 包含`中国`或包含`人民`
||
`中国 and not 日本` | 包含 `中国`不包含`日本`
`中国 not 日本` | 包含 `中国`不包含`日本`
`中国 - 日本` | 包含 `中国`不包含`日本`

## 原子查询
诸如上面 `中国`，`人民`这样的部分属于原子查询，原子查询还可以是正则表达式(后面介绍)。

原子查询可以加引号来明确，例如 `"中国-2025年GDP"`，因为如果不加引号，就会解释为 `中国 - 2025年GDP`。引号包起来的部分可以包含特殊字符与C语言转义字符。

## 指定字段

fieldname(..表达式..) 表示仅在 fieldname 字段上检查该表达式。

系统内置三个字段，一个是通用字段，另外两个是 title 和 content。未指定字段的部分作为通用字段处理。

## near 语法

(表达式1) `near/num` (表达式2) | `表达式1` 和 `表达式2` 不分前后，之间的距离不超过 num
-----------------------------|-----------------------------------
(表达式1) `near/+num` (表达式2) | `表达式1` 在 `表达式2` 之前，之前的距离不超过 num
(表达式1) `near/-num` (表达式2) | `表达式1` 在 `表达式2` 之后，之前的距离不超过 num

其中 num 的单位是：对于中文，每个中文字符为1，对于英文/数字，每个英文单词/数字为1。例如 computer , 10086 , varname_123 均视为 1 个距离单位。very_big_computer_long_word_9558493 也视为 1 个距离单位。计算距离时会排除特殊字符和标点符号、空格等。


### 链式 near

链式 near 的语义是多个 near 的 and，例如：

```
中国 near/3 人民 near/5 勤劳 near/7 勇敢
```

等价于：
```
中国 near/3 人民 and 人民 near/5 勤劳 and 勤劳 near/7 勇敢
```

### 复合 near

```
(中国 or 中华 or 华夏) near/3 (人民 or 百姓) near/5 (勤劳 or 勤奋) near/7 (勇敢 or 尚武)
```

### 更复杂的 near

不推荐使用复杂的 near，这会导致人类难以理解并且计算机也难以处理的结构。

```
(中国 or 中华 or 华夏) near/3 ( (人民 or 百姓) near/5 (勤劳 or 勤奋) or (历史悠久 or 幅员辽阔))
```

near 的子表达中不能包含 near 和 or 之外的其它表达式，并且子表达式同时包含 near 和 or 时，or 的另一个部分只能是括号括起来的全是**原子查询**的 or 序列。

## 正则表达式
原子查询词可以是正则表达式，简单的正则表达式用 {{....}} 括起来，还可以用 [ ... ] 表达括起来的复合正则表达式。例如：

`{{(\(010\)|010)-?{[0-9]{8}}}}` 表示北京的电话号码。

### 复合正则表达式

```
[{{https?://(([\w\d_-]+)\.)+([\w\d_-]+)}} and not {{.*\.com}}]
```
表示除了以 `.com` 结尾的之外的所有网站首页。

### [] 内的 near
$$[\\;]$$ 内的复合正则表达式也支持 near，语法跟 $$[\\;]$$ 外的 near 相同，语义也类似，但处理方式不同， $$[\\;]$$ 内的 near 会编译为 regex DFA，性能更高，但是编译耗时更长，DFA 的内存开销也会更大。

优势在于， $$[\\;]$$ 内的 near 的子表达式可以是高频词，从而相比 $$[\\;]$$ 外的 near，会大幅降低 rule_id 的召回，减小需要验证的规则数量，从而提升总体性能。

## 规则库源文件

规则库源文件是个文本文件，包含多条以上规则，每行一条规则，`#` 开始的行是注释，空行直接跳过，可以使用 '\\' 作为续行符将太长的行分成多行。

每行规则可以带有关联数据，规则本身和关联数据以 tab '\\t' 分隔，关联数据用来将规则ID和业务数据关联起来，规则本身是第一列，关联数据是第二列。

## 编译规则库

`rule_db_build.exe` 是规则库编译程序（linux 上也有 .exe 后缀），用来将规则库源文件编译为二进制规则数据库，命令行用法：

```
rule_db_build.exe <选项>  <规则源码文件>
```

 选项 | 参数 | 解释说明
------| --------|-------------------------
-h    | 无参数   | 打印帮助信息
-o    | 输出目录 | 编译输出的二进制规则数据库，包含多个文件，目录中的现存文件会被覆盖，如果目录不存在会自动创建。<br>建议编译前删除输出目录以获得干净的编译结果。
-F    | 字段名   | 添加一个用户自定义字段，可以包含多个 -F
-q    | 无参数   | 不打印进度及不重要的警告信息等
-W    | 文件名   | 指定一个词频文件(word \\t freq)，用来优化 allof/anyof<br>所有词频相关的错误都会被忽略

rule_db_build.exe 执行完后，会在输出目录下生成一些文件，其中包含一个 Makefile，再进入该目录执行 make 才会将数据库完全建好。我们提供了 rule_db_build.sh 脚本用来自动化这个流程，其用法与 rule_db_build.exe 相同，额外会自动运行 make 将数据库完全建好。

输出目录中包含一个重要文件 rule_id_map.txt，其中内容(tab分隔)第一列是 rule_id，第二列是规则库源文件中的业务关联数据。第一列的 rule_id 总是从 0 到 n-1, n 为成功编译的规则，编译失败的规则被自动忽略，不分配 rule_id，不会出现在 rule_id_map.txt 中。

## API 示例用法

```c++
#include <rule_db.h>
RuleDatabase db; // 打开后是只读对象，可多线程使用
if (!db.open(dbdir)) {
    printf("FATAL: db.open(%s) = %s\n", dbdir, db.strerr());
    return 1;
}
RuleMatcher matcher; // 可复用 matcher 对象，减少内存分配次数，不可多线程使用
if (!matcher.init(db)) {
    printf("FATAL: matcher.init(%s) = %s\n", dbdir, matcher.strerr());
    return 1;
}
// true 表示对未知字段不报错，而是将所有未知字段拼接后作为通用字段内容
// 默认就是 true，这里只是明确设置一下
matcher.ignore_unknown_fields(true);
map<string, shared_ptr<string> > doc; // 文档就是一个简单的 map
// 1. rule 中定义的无 fieldname 的表达式不会自动匹配所有已知字段
// 2. 未知字段匹配无字段名的表达式(ignore_unknown_fields(true))
auto title = make_shared<string>(doc_from_db.title);
auto content = make_shared<string>(doc_from_db.content);
doc["title"]    = title;
doc["content"]  = content;
// 如果想让规则库中 未指定字段的表达式 匹配 title 和 content，
// 就在这里故意指定规则库中未定义的字段名 .title 和 .content 来
// 触发前述“未知字段匹配无字段名的表达式”的语义行为
doc[".title"]   = title;
doc[".content"] = content;
if (!matcher.match(doc)) {
    printf("matcher.match(doc) = %s", matcher.strerr());
    return 1;
}
auto& matchset = matcher.get_result();
if (matchset.empty()) {
    // 没有命中任何规则
}
// 初始化时可从 dbdir/rule_id_map.txt 中将 rule_id 与业务数据关联起来
// 这里仅打印命中的规则
for (int rule_id : matchset)
    printf(" %d", rule_id);
```

链接时需要加 -lruledb-r （后缀 -r 表示 release 版）
