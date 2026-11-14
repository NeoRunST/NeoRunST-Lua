-- smoke.lua — Lua 5.5 on NeoRunST 冒烟测试
-- 跑法: lua.nep test/smoke.lua  (全部通过最后打印 OK)

local function eq(a, b, tag)
  if a ~= b then
    print("FAIL [" .. tostring(tag) .. "] got=" .. tostring(a) .. " want=" .. tostring(b))
    os.exit(1)
  end
end

-- ── 基础值与运算 ──
eq(1 + 2 * 3, 7, "arith")
eq(7 // 2, 3, "idiv")
eq(7 % 3, 1, "mod")
eq(2 ^ 10, 1024, "pow")
eq(1 / 3 ~= 0.3333333333333333, false, "float")
eq(-0.0 == 0.0, true, "negzero")
eq(0x10, 16, "hex")
eq(10 // 0, math.huge, "idiv0")
eq(3.0, 3, "intfloat-eq")
eq(math.maxinteger + 1 == math.mininteger, true, "intwrap")

-- ── 字符串 ──
eq("ab" .. "cd" .. 12, "abcd12", "concat")
eq(#"hello", 5, "len")
eq(("hello world"):sub(7), "world", "method-sub")
eq(string.format("%d|%s|%.2f|%x|%g", -42, "hi", 3.14159, 255, 1/3),
   "-42|hi|3.14|ff|0.333333", "format")
eq(string.format("%.14g", math.pi), "3.1415926535898", "format14g")
eq(string.rep("ab", 3), "ababab", "rep")
eq(string.upper("aBc1"), "ABC1", "upper")
eq(("  hi  "):match("^%s*(.-)%s*$"), "hi", "trim-pattern")
eq(("k=v"):match("(%w+)=(%w+)"), "k", "match-cap")
eq(string.gsub("hello world", "o", "0"), "hell0 w0rld", "gsub-count")
eq(("%d"):format(2^31), "2147483648", "bigint-format")
eq(string.byte("A"), 65, "byte")
eq(string.char(72, 105), "Hi", "char")

-- ── 表 / 元表 ──
local t = {10, 20, 30, x = "kv"}
eq(t[2], 20, "table-index")
eq(t.x, "kv", "table-hash")
eq(#t, 3, "table-len")
t[#t + 1] = 40
eq(#t, 4, "table-append")
eq(table.concat({1, 2, 3}, "-"), "1-2-3", "table-concat")
local mt = setmetatable({}, {__index = function(_, k) return k .. "!" end,
                             __add = function(a, b) return 99 end,
                             __tostring = function() return "OBJ" end})
eq(mt.foo, "foo!", "__index-func")
eq(mt + 1, 99, "__add")
eq(tostring(mt), "OBJ", "__tostring")
eq(table.remove({1, 2, 3}), 3, "table-remove")
eq(select("#", table.unpack({1, 2, 3})), 3, "unpack")
local sorted = {3, 1, 2}
table.sort(sorted)
eq(sorted[1] .. sorted[2] .. sorted[3], "123", "sort")

-- ── 控制流 / 闭包 ──
local acc = {}
for i = 1, 3 do acc[i] = i * i end
eq(acc[3], 9, "numeric-for")
eq(acc[4], nil, "for-bound")
local i = 0
repeat i = i + 1 until i >= 3
eq(i, 3, "repeat")
local sum = 0
for k, v in pairs({a = 1, b = 2}) do sum = sum + v end
eq(sum, 3, "pairs")
local np = 0
for _, v in ipairs({5, 6, 7}) do np = np + v end
eq(np, 18, "ipairs")
local function counter()
  local n = 0
  return function() n = n + 1; return n end
end
local c1, c2 = counter(), counter()
c1(); c1()
eq(c1(), 3, "closure-upvalue")
eq(c2(), 1, "closure-indep")
local function vf(...) return select("#", ...), ... end
eq(vf("a", nil, "c"), 3, "varargs-nil")
eq((function(...) return ... end)("x", "y"), "x", "multi-tail")
local goto_done = 0
do
  local j = 0
  ::top::
  j = j + 1
  if j < 3 then goto top end
  goto_done = j
end
eq(goto_done, 3, "goto")

-- ── 错误处理 ──
eq(pcall(function() error("boom") end), false, "pcall-false")
local ok, err = pcall(function() error({code = 42}) end)
eq(ok, false, "pcall-table")
eq(err.code, 42, "error-object")
eq(pcall(function() return 1 + {} end), false, "pcall-typeerr")
eq(select(2, pcall(error, "msg", 0)), "msg", "error-level0")
local protected = pcall(function()
  local missing = nosuchglobalfunction()
  return missing
end)
eq(protected, false, "pcall-nilcall")

-- ── 协程 ──
local co = coroutine.create(function(a)
  local b = coroutine.yield(a * 2)
  return a + b
end)
eq(select(2, coroutine.resume(co, 21)), 42, "coro-yield")
eq(coroutine.status(co), "suspended", "coro-status")
eq(select(2, coroutine.resume(co, 8)), 29, "coro-resume")
eq(coroutine.status(co), "dead", "coro-dead")
local gen = coroutine.wrap(function() for i = 1, 3 do coroutine.yield(i) end end)
eq(gen() + gen() + gen(), 6, "coro-wrap")

-- ── 数学库 ──
eq(math.floor(3.7), 3, "floor")
eq(math.ceil(-3.2), -3, "ceil")
eq(math.abs(-5), 5, "abs")
eq(math.max(1, 9, 4), 9, "max")
eq(math.min(1, 9, 4), 1, "min")
eq(math.sqrt(144), 12.0, "sqrt")
eq(math.tointeger(3.0), 3, "tointeger")
eq(math.type(1), "integer", "math-type-int")
eq(math.type(1.0), "float", "math-type-float")
eq(math.fmod(7, 3), 1.0, "fmod")
eq(math.log(100, 10) > 1.999 and math.log(100, 10) < 2.001, true, "log10-base")
eq(math.exp(0), 1.0, "exp0")
eq(math.pi > 3.14 and math.pi < 3.15, true, "pi")
local fr, ex = math.frexp(16.0)
eq(fr, 0.5, "frexp-m")
eq(ex, 5, "frexp-e")
eq(math.ldexp(0.5, 5), 16.0, "ldexp")
eq(math.huge > 2 ^ 1023, true, "huge")
eq(0 / 0 ~= 0 / 0, true, "nan-neq")
math.randomseed(7)
local r = math.random(1, 1000)
eq(r >= 1 and r <= 1000, true, "random-range")
eq(math.ult(1, 2), true, "ult")

-- ── tostring / tonumber ──
eq(tostring(1.0), "1.0", "tostring-float")
eq(tostring(-17), "-17", "tostring-int")
eq(tostring(nil), "nil", "tostring-nil")
eq(tostring(true), "true", "tostring-bool")
eq(tonumber("  0x1A  "), 26, "tonumber-hex")
eq(tonumber("3.5e2"), 350.0, "tonumber-exp")
eq(tonumber("abc"), nil, "tonumber-bad")
eq(tonumber("0x1p4"), 16.0, "tonumber-hexfloat")
eq(tonumber("ff", 16), 255, "tonumber-base16")
eq(tonumber("z", 36), 35, "tonumber-base36")

-- ── GC ──
collectgarbage()
eq(type(collectgarbage("count")), "number", "gc-count")

-- ── os / io 基础 ──
eq(type(os.time()), "number", "os-time")
eq(type(os.clock()), "number", "os-clock")
eq(os.date("%H", 3661), "01", "date-H")
eq(os.date("%Y-%m-%d %H:%M:%S", 0), "1970-01-01 00:00:00", "date-epoch")
eq(os.difftime(10, 4), 6.0, "difftime")
eq(type(io.write), "function", "io-write")
io.write("")
eq(os.getenv("NOPE"), nil, "getenv-nil")

-- ── utf8 ──
eq(utf8.len("héllo"), 5, "utf8-len")
eq(utf8.codepoint("α"), 945, "utf8-codepoint")
eq(utf8.char(945, 946), "αβ", "utf8-char")

-- ── 长字符串 / 转义 ──
local long = [==[a]]b]==]
eq(long, "a]]b", "longstring")
eq("\x41A" .. "\n" == "AA\n", true, "esc-hex")
eq(#"\u{1F600}", 4, "esc-u8")

print("smoke: all pass (" .. _VERSION .. ")")
