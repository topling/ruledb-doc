# 整数字段

规则数据库仅包含文本匹配功能，原则上不支持文本之外的其它类型，但是我们可以通过一些技巧，实现非常高效的支持。

其原理是：我们可以将整数编码为 utf8 字符，然后利用字符串匹配来实现范围与逻辑判断。

举一个典型的例子：输入的 doc 是一个人的信息，要限定 **性别**、**年龄**、**收入**，并且这三个字段是 **与(and)** 关系。

年龄我们用 1/0 表示男女，年龄、收入都是整数。现在要表达 性能为男 and 年龄为 25 到 35 岁，收入为 12000 到 18000 的这样一个限制条件。

我们增加一个伪字段 `gender_age_income` ，其内容是三个字段的**串接**，该查询条件就可以表达为一条规则：

```
gender_age_income({{1[\i{25}-\i{48}][\i{12000}-\i{18000}]}}) <tab> rule_1
gender_age_income[{{1[\i{25}-\i{48}][\i{12000}-\i{18000}]}}] <tab> rule_2
gender_age_income {{1[\i{25}-\i{48}][\i{12000}-\i{18000}]}}  <tab> rule_3
```
其中 `\i{}` 是 **Topling 正则引擎** 支持的 **10 进制 unicode** 码点。
另外性别男女直接用字符 `1`/`0` 来表达，而是不是 ascii 的 `0x01` 和 `0x00`。

rule_`1`/`2`/`3` 是关联的业务数据，将规则关联到业务逻辑。

三种写法的区别：
**rule_1**: 作为普通规则按照字符串匹配，可匹配该字段的任意子串。
**rule_2**: 必须是全匹配，可以包含多个正则表达式的 **并**、**交**、**差**、**串接** 等。
**rule_3**: 是 **rule_2** 更简洁的形式，因为此处无复杂操作，故可以简写。

## 优势
前面的 gender_age_income 方案有什么优势呢？比下面这种简单直接的方案好在哪里呢？
```
gender(1) and age({{[\i{25}-\i{48}]}}) and income({{[\i{12000}-\i{18000}]}})
```
答案是：这种简单直接的方案，在召回规则id时，每条类似的规则都会被召回，
例如 `gender(1)` 也就是`性别男` 的规则可能占了规则总量的 50%，那么就有 50% 的规则
被召回，然后在验证阶段排除。

前面的 gender_age_income 方案直接在召回阶段就选中了精确匹配的规则，大幅减小了需要验证的规则数量。

## 创建规则库

创建规则库时，要自定义 gender_age_income 字段：

```
rule_db_build.sh -F gender_age_income -o dbdir rule.txt
```

## 查询规则库
查询规则库时，创建 doc 也要添加 gender_age_income 字段：

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
map<string, shared_ptr<string> > doc; // 文档就是一个简单的 map
if (auto encoded = matcher.encode_uint_fields({gender, age, income})) {
    doc["gender_age_income"] = encoded;
} else {
    printf("ERROR: Encode failed: %s\n", matcher.strerr());
}
```

我们提供了 RuleMatcher::encode_uint_fields() 这个函数用来方便地将整数序列转化为
utf8 字符串。目前仅支持用单个 unicode 码点表示整数，支持的整数的最大值为 0x10FFFF。
