# 简介
Topling 规则数据库的功能是：将 $$\color{#DC143C}{预定义规则}$$ 作为数据库，将 $$\color{#DC143C}{输入文档}$$ 作为查询条件，搜索预定义的规则。

应用场景包括：敏感词检测、广告定向、数据分类、事件分流、自动打标签等，比现存其它方案
(例如drools/ElasticSearch percolator等)性能高至少一个数量级，规则越多，优势越突出，支持
百万条以上的规则，每条规则的表达式可以任意复杂。表达式的原生数据类型是字符串，也能
高效地支持 [数值](integer-fields.md) 与 [geo空间数据](geo-fields.md)，以及不限宽度的[整数(范围)](bigint.md)，[实数(范围)](realnum.md)，支持[联合索引](realnum.md#%E4%BE%8B%E7%BB%8F%E7%BA%AC%E5%BA%A6)。

Topling 规则数据库是商业软件，虽未开源也未开放下载，但底层的正则表达式引擎、正则语言代数运算、NFA/DFA lib 是[开源的](https://github.com/topling/topling-ark)。

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

fieldname(..表达式..) 表示仅在 fieldname 字段上检查该表达式，匹配该字段的任意子串。

fieldname[..表达式..] 表示仅在 fieldname 字段上检查该表达式，匹配该字段的全部内容。全匹配支持多字段[联合索引](realnum.md#%E4%BE%8B%E7%BB%8F%E7%BA%AC%E5%BA%A6)，拥有完备的多维空间搜索功能。

系统内置两个字段，content 和 title，content 是默认字段，未指定字段的表达式默认当做 content。

## near 语法

(表达式1) `near/num` (表达式2) | `表达式1` 和 `表达式2` 不分前后，之间的距离不超过 num
-----------------------------|-----------------------------------
(表达式1) `near/+num` (表达式2) | `表达式1` 在 `表达式2` 之前，之前的距离不超过 num
(表达式1) `near/-num` (表达式2) | `表达式1` 在 `表达式2` 之后，之前的距离不超过 num

其中 num 的单位是：对于中文，每个中文字符为1，对于英文/数字，每个英文单词/数字为1。例如 computer , 10086 , varname_123 均视为 1 个距离单位。very_big_computer_long_word_9558493 也视为 1 个距离单位。计算距离时会排除特殊字符和标点符号、空格等。

一个`\0`占一个距离单位，如果想人为添加词距，可在文本中加入`\0`。

### (1) 多词项 near
```
(中国 or 中华 or 华夏) near/3 (人民 or 百姓)
```

### (2) 链式 near

链式 near 的语义是多个 near 的串接，例如：

```
中国 near/+3 人民 near/+5 勤劳 near/+7 勇敢
```

表示这 4 个词项按顺序出现并满足距离约束。


### (3) 复合 near

```
(中国 or 中华 or 华夏) near/3 (人民 or 百姓) near/5 (勤劳 or 勤奋) near/7 (勇敢 or 尚武)
```

### (4) 更复杂的 near

不推荐使用复杂的 near，这会导致人类难以理解并且计算机也难以处理的结构。

```
(中国 or 中华 or 华夏) near/3 ( (人民 or 百姓) near/5 (勤劳 or 勤奋) or (历史悠久 or 幅员辽阔) and 地大物博)
```

near 的子表达式可以是 or 表达式，如果还包含 near,and,not 等子表达式，会通过分配率提到外层。

其中 (2),(3),(4) 这三种复杂 near 都是对其作为正则语言的语义的弱化以保证性能。

## 正则表达式
原子查询词可以是正则表达式，简单的正则表达式用 {{....}} 括起来，还可以用 [ ... ] 表达括起来的复合正则表达式。例如：

`{{(\(010\)|010)-?{[0-9]{8}}}}` 表示北京的电话号码。

### 复合正则表达式

```
[{{https?://(([\w\d_-]+)\.)+([\w\d_-]+)}} and not {{.*\.com}}]
```
表示除了以 `.com` 结尾的之外的所有网站首页。

对于指定字段的场景，[ ] 内只包含一个正则表达式时，`fieldname[{{ ...表达式... }}]` 全匹配可简写为 `fieldname{{ ...表达式... }}`。

### [] 内的 near
$$[\\;]$$ 内的复合正则表达式也支持 near，语法跟 $$[\\;]$$ 外的 near 相同，语义也类似，但处理方式不同， $$[\\;]$$ 内的 near 会编译为 regex DFA，性能更高，但是编译耗时更长，DFA 的内存开销也会更大。

优势在于， $$[\\;]$$ 内的 near 的子表达式可以是高频词，从而相比 $$[\\;]$$ 外的 near，会大幅降低 rule_id 的召回，减小需要验证的规则数量，从而提升总体性能。

### 串接
复合正则表达式支持串接操作 `.` ，类似字符串的直接串接，在多维搜索的场景下等效于笛卡尔积(参考[大整数](bigint.md))，例如：

```
[ ({{\w+}} - {{[0-9].*}} - {{.*__.*}}) . "=" . ({i{-50,70}} | {i{100,9876543210}}) ]
```
匹配 `myvar_123=87654321`, 限制变量名不能以 `[0-9]` 开头，不能包含连续两个以上的 `_`，赋予的值只能属于 [-50,70] 或 [100,9876543210] 区间。

`{i{min,max}}` 是[大整数](bigint.md)区间的语法。

### 非贪婪串接
`A : B` 为非贪婪串接：最短的 A 串接 B，例如 `{{a.*}} : b`，只匹配 `abbbb` 中的 `ab`。值得注意的是，非贪婪串接也发生在编译时而非运行时。

### 重复
[ ] 内的符合正则表达式支持标准正则表达式的**重复**语法 `*`, `+`, `?`, `{min,max}`, `{num}`，例如以分号 `;` 加换行结束的 3到8 个上述变量赋值表达式：

```
(({{\w+}} - {{[0-9].*}} - {{.*__.*}}) . "=" . ({i{-50,70}} | {i{100,9876543210}}).";\n"){3,8}
```

重复也支持**非贪婪**，语法与传统正则表达式的非贪婪相同，例如 `{{[a-z]}}*?{{ab}}`，同样发生在编译时。

### 频次限定
**单个词项出现的频次：**
```
中国 == {3} and 人民 >= {5}
```

**词项表达式必须在左边：**
```
# 表示 "123" 这个词(按英文单词/数字边界切分后) 的出现次数正好123次
# 并且 "456" 这个词 的出现次数低于456次(出现0次就是未出现)
123 == {123} and 456 < {456}
```

**多个词项数量及频次**：**数量**指不同词的个数(每个词无论出现多少次都只计数一次)

词项列表复用了布尔逻辑 or(|) 的表达式，同样必须出现在左边，并且要用括号括起来：

列表中的词至少出现 2 个并且总频次至少 7：
```
(中国|中华|华夏|china) >= {2,7}
```

列表中的词至少出现 2 个，不管总频次多少：
```
(中国|中华|华夏|china) >= {2,}
```

列表中的词总频次至少 8，不管出现了多少个不同的词
```
(中国|中华|华夏|china) >= {,8}
```

列表中的词正好出现 2 个，不管总频次多少：
```
(中国|中华|华夏|china) == {2,}
```

列表中的词总频次正好 8，不管出现了多少个不同的词：
```
(中国|中华|华夏|china) == {,8}
```

支持的所有比较操作符： `==`, `!=`, `>=`, `>`, `<=`, `<`，其中 `>=` 是最常用的。

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
-i    | 联合索引 | s{sepa}(field1,field2,...)，其中 s{sepa} 是可选的，省略时 sepa 是 `\0`, [示例用法](https://github.com/topling/ruledb-doc/blob/main/realnum.md#%E4%BE%8B%E7%BB%8F%E7%BA%AC%E5%BA%A6)
-o    | 输出目录 | 编译输出的二进制规则数据库，包含多个文件，<br>目录中的现存文件会被覆盖，如果目录不存在会自动创建。<br>建议编译前删除输出目录以获得干净的编译结果。
-F    | 字段名   | 添加一个用户自定义字段，可以包含多个 -F
-q    | 无参数   | 不打印进度及不重要的警告信息等
-s    | 文件名   | 停止词文件，每行一个停止词
-W    | 文件名   | 指定一个词频文件(word \\t freq)，用来优化 allof/anyof<br> freq <= 0 会被认为是停止词<br>所有词频相关的错误都会被忽略

rule_db_build.exe 执行完后，会在输出目录下生成一些文件，其中包含一个 Makefile，再进入该目录执行 make 才会将数据库完全建好。我们提供了 rule_db_build.sh 脚本用来自动化这个流程，其用法与 rule_db_build.exe 相同，额外会自动运行 make 将数据库完全建好。

输出目录中包含一个重要文件 rule_id_map.txt，其中内容(tab分隔)第一列是 rule_id，第二列是规则库源文件中的业务关联数据。第一列的 rule_id 总是从 0 到 n-1, n 为成功编译的规则，编译失败的规则被自动忽略，不分配 rule_id，不会出现在 rule_id_map.txt 中。

## API 示例用法
这里是一个[完整的示例程序](match_doc.cpp)。

### 1. 打开数据库
```c++
#include <rule_db.h>
RuleDatabase db; // 打开后是只读对象，可多线程使用
if (!db.open(dbdir)) {
    printf("FATAL: db.open(%s) = %s\n", dbdir, db.strerr());
    return 1;
}
```

### 2. 关联业务数据
```c++
// 需要读取 dbdir/rule_id_map.txt 将 rule_id 与业务数据关联起来，
// db.load_rule_map 是个实现该功能的工具函数
vector<int> rule_id_to_category_id; // 业务类别ID
rule_id_to_category_id.reserve(db.total_rules()); // 写法1
rule_id_to_category_id.resize (db.total_rules()); // 写法2
// 必须先 open db 再 load_rule_map
int ret = db.load_rule_map([&](int rule_id, const char* category) {
    // category 是业务数据，可以任意复杂，这里仅以最简单的情形为例
    assert(rule_id == rule_id_to_category_id.size()); // 写法1, 必然相等
    assert(rule_id  < rule_id_to_category_id.size()); // 写法2
    rule_id_to_category_id.push_back(atoi(category)); // 写法1, 无需 rule_id
    rule_id_to_category_id[rule_id] = atoi(category); // 写法2
});
if (ret < 0) {
    printf("FATAL: db.load_rule_map() = %s\n", db.strerr());
    return 1;
}
```

### 3. 创建匹配器，设置匹配选项
db 的生存期必须覆盖 matcher 的生存期。
```c++
RuleMatcher matcher; // 可复用 matcher 对象，减少内存分配次数，不可多线程使用
if (!matcher.init(db)) {
    printf("FATAL: matcher.init(%s) = %s\n", dbdir, matcher.strerr());
    return 1;
}
// true 表示对未知字段不报错，而是将所有未知字段拼接后作为通用字段内容
// 默认就是 true，这里只是明确设置一下
matcher.ignore_unknown_fields(true);
```

### 4. 构造文档对象
1. rule 中定义的无 fieldname 的表达式会被当做 content 字段的表达式
1. 未知字段会按 content 字段进行匹配(需要ignore_unknown_fields(true))

### 4.1. 最简单的情况
```c++
map<string, shared_ptr<string> > doc; // 文档就是一个简单的 map
auto title = make_shared<string>(doc_from_db.title);
auto content = make_shared<string>(doc_from_db.content);
doc["title"]    = title;
doc["content"]  = content;
// 如果想让规则库中 未指定字段的表达式 匹配 title，
// 就在这里故意指定规则库中未定义的字段名 .title
// 触发前述“未知字段按 content 字段匹配”的语义行为
doc[".title"]   = title;
```
### 4.2. ComplexQuery
文档对象作为一个“搜索匹配的规则”的查询器，它可以更加复杂，每个字段都可以是前述的“[复合正则表达式](#复合正则表达式)”。
db.match 对规则中的正则表达式(对应的DFA)和 doc 中的正则表达式(对应的DFA)求交集来实现搜索。
```c++
map<string, RuleMatcher::ComplexQuery> doc; // 文档就是一个简单的 map
auto title = make_shared<string>(doc_from_db.title);
auto content = make_shared<string>(doc_from_db.content);
doc["title"]   = {title, false};
doc["content"] = {content, false};
doc[".title"]  = {title, false}; // 见 4.1.
doc["gender"]  = {make_shared<string>("1"), false};
doc["age"]     = {make_shared<string>("28"), false};
doc["income"]  = { // 收入在这个范围内浮动
    make_shared<string>("{i{15000,23000}}"), // text 成员
    true, // is_regex 成员
};
doc["interesting"] = {
    make_shared<string>("运动|电影|美食"), // text 成员
    true, // is_regex 成员
};
doc["books"] = {
    make_shared<string>("红楼梦|水浒|红与黑"), // text 成员
    true, // is_regex 成员
};
```
### 4.3. Json 字符串作为 doc
```c++
const char* docjson = R"json({
    "title": "...",
    "content": "...",
    ".title": "...",
    "gender": "1",
    "age": "28",
    "income": {
        "text": "{i{15000,23000}}",
        "is_regex": true
    },
    "interesting": {
        "text": "运动|电影|美食",
        "is_regex": true
    },
    "books": {
        "text": "红楼梦|水浒|红与黑",
        "is_regex": true
    }
})json";
// 注意: match(c_str_docjson) 是禁止调用的，会编译出错
if (!matcher.match(docjson, strlen(docjson))) {
    printf("matcher.match(doc) = %s", matcher.strerr());
    return 1;
}
// 或(编译器支持 string_view 时)
if (!matcher.match(string_view(docjson))) {
    printf("matcher.match(doc) = %s", matcher.strerr());
    return 1;
}
// 或(编译器不支持 string_view 时)，应尽可能使用前两个重载
if (!matcher.match(string(docjson))) {
    printf("matcher.match(doc) = %s", matcher.strerr());
    return 1;
}
```

### 5. 执行匹配
```c++
if (!matcher.match(doc)) {
    printf("matcher.match(doc) = %s", matcher.strerr());
    return 1;
}
auto& matchset = matcher.get_result();
if (matchset.empty()) {
    // 没有命中任何规则
}
```

### 6. 打印匹配结果
```c++
for (int rule_id : matchset) {
    printf(" %d(category %d)", rule_id, rule_id_to_category_id[rule_id]);
    if (SomeCondition(rule_id_to_category_id[rule_id])) {
        for (auto& [fieldname, pos_vec] : matcher.get_match_pos(rule_id)) {
            // print fieldname & pos_vec
        }
    }
}
```
get_match_pos 涉及匹配路径的回溯，单次调用开销一般在30微秒($$30\mu s$$)级别。
虽然相比同类方案有数量级的优势，但相比 ruledb 自身“判定命中”的性能是不可忽视的，建议仅在必要时时调用。

### 7. 链接
链接时需要加 -lruledb-r （后缀 -r 表示 release 版）
