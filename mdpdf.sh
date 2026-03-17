ordinal_list=(
    "第一章" "第二章" "第三章" "第四章" "第五章"
    "第六章" "第七章" "第八章" "第九章" "第十章"
    "第十一章" "第十二章" "第十三章" "第十四章" "第十五章"
)
filelist=(
  README.md
  bigint.md
  realnum.md
  integer-fields.md
  ruledbjava.md
  rest-api.md
  design.md
  notes.md
)
args=(
    --toc --pdf-engine=xelatex -s
    #--highlight-style=pygments # 代码块无围栏无底色
    --highlight-style=tango # 代码块有很浅的浅灰底色
    --top-level-division=chapter
    #-V tables
    -V mainfont="AR PL UMing CN"
    -V CJKmainfont="Noto Sans CJK SC"
    # margin: 正文与边界的间距，页眉页脚是位于 margin 内的
    # headsep: 页眉与正文的间距（默认约 25pt，改小可让页眉更贴近正文）
    # headheight: 页眉高度
    # footskip: 调整页脚与正文底部的间距
    # left:
    # right
    -V geometry:margin=0.8in,left=0.8in,right=1in,headsep=0.4in,footskip=0.4in,headheight=15pt
    #-V linestretch=1.5
    #-V toccolor="[rgb]{0, 0.4, 0.8}"
    -V colorlinks=true -V urlcolor="[rgb]{0, 0.4, 0.8}"
    -H header.tex
)
set -x
function merge() {
cat << "EOF"
---
title: "RuleDB 用户手册"
author: "Lei Peng"

---
EOF
    for ((i=0; i < ${#filelist[@]}; i++)) ; do
        file=${filelist[i]}
        ordinal=${ordinal_list[i]}
        sed -e '1s:\(#.*</a>\) \(.*\):\1'"  ${ordinal}：  "'\2:'  -e 's:\\|:|:' $file
        echo
    done
}
merge > tmp.md
cat tmp.md | pandoc -o ruledb-manual.pdf "${args[@]}"
