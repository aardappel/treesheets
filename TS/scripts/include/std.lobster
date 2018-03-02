// standard functions that build on top of the builtin functions useful for almost all programs

def map(xs, fun):
    r := vector_reserve(typeof return, xs.length)
    for(xs) x, i:
        r.push(fun(x, i))
    r

def map2(xs, ys, fun): map xs.length: fun(xs[_], ys[_])

def filter(xs, fun):
    r := []
    for(xs) x, i:
        if fun(x, i):
            r.push(x)
    r

def filterindices(l, f):
    r := []
    for(l) x, i:
        if f(x, i):
            r.push(i)
    r

def exists(xs, fun):
    for(xs) x, i:
        if fun(x, i):
            return true
    false

def forever(fun): while true: fun()

def fold(xs, acc, fun):
    for xs: acc = fun(acc, _)
    acc

def fold2(xs, acc1, acc2, fun):
    for xs:
        acc1, acc2 = fun(acc1, acc2, _)
    return acc1, acc2

def reduce(xs, fun):
    assert xs.length
    acc := xs[0]
    for(xs.length - 1) i: acc = fun(acc, xs[i + 1])
    acc

def connect(xs, fun):
    map(max(0, xs.length - 1)) i: fun(xs[i], xs[i + 1])

def reducerev(xs, fun):
    assert xs.length
    acc := xs[xs.length - 1]
    for(xs.length - 1) i: acc = fun(xs[xs.length - i - 2], acc)
    acc

def find(xs, fun):
    for(xs) x, i: if fun(x): return i
    -1

// return element for which fun returns biggest float value.
def find_best(xs, fun):
    best := 0.0
    i := -1
    for(xs) x, j:
        v := fun(x)
        if i < 0 or v > best:
            i = j
            best = v
    i

def weightedpick(list, zero, wf):
    // FIXME: "zero" is an easy way to select float vs int implementation, explicit template
    // arg would be better here.
    weights := fold(list, zero): _a + wf(_x)
    assert(weights)
    pick := rndfloat() * weights
    acc := zero
    i := list.find():
        acc += wf(_)
        pick < acc
    list[i]

def sum(xs):     fold(xs, 0): _x + _y
def product(xs): fold(xs, 1): _x * _y

def zip(xs, ys): map xs.length: [ xs[_], ys[_] ]

def reverse(xs, fun): for(xs.length) i: fun(xs[xs.length - i - 1])
def reverselist(xs): map(xs.length) i: xs[xs.length - i - 1]

def flatten(xs): fold(xs, []): append(_a, _b)

def split(l, f):
    r := [ [], [] ]
    for(l) e: r[f(e)].push(e)
    r

def qsort(xs, lt):
    if xs.length <= 1:
        xs
    else:
        pivot := xs[0]
        tail := xs.slice(1, -1)
        f1 := tail.filter():  lt(_, pivot)
        f2 := tail.filter(): !lt(_, pivot)
        append(append(qsort(f1, lt), [ pivot ]),
                      qsort(f2, lt))

def qsort_in_place(xs, lt):
    def rec(s, e):
        l := e - s
        if l > 1:
            pivot := xs[s]
            sp := s + 1
            ep := e
            while sp < ep:
                c := xs[sp]
                if lt(c, pivot):
                    xs[sp - 1] = xs[sp]
                    sp++
                else:
                    xs[sp] = xs[--ep]
                    xs[ep] = c
            xs[--sp] = pivot
            rec(s, sp)
            rec(ep, e)
    rec(0, xs.length)

def insertion_sort(xs, lt):
    for(xs) key, i:
        if i:
            j := i
            while j > 0 and lt(key, xs[j - 1]):
                xs[j--] = xs[j - 1]
            xs[j] = key

def insert_ordered(xs, x, lt):
    for(xs) key, i:
        if lt(x, key):
            xs.insert(i, x)
            return
    xs.push(x)

def randomize(xs):
    // Simple and effective, but remove makes it expensive.
    for(xs.length) i: xs.push(xs.remove(rnd(xs.length - i)))
    xs

def nest_if(c, nest, with):
    if c: nest(with)
    else: with()

def return_after(v, f):
    f()
    v

def do(f): f()  // Useful to create a scope where there is none.

def forbias (num, bias,  fun): for num: fun(_ + bias)
def forscale(num, scale, fun): for num: fun(_ * scale)

def forrange    (a, b, fun): forbias(b - a,     a, fun)
def forrangeincl(a, b, fun): forbias(b - a + 1, a, fun)

// HOFs that work on other HOFs:

def collect(hof):
    list := []
    hof(): list.push(_)
    list

// coroutine utility functions:

// generally don't want to use this function, since you'd be better off directly using the HOF the coroutine is based on
def coroutine_for(co, f):
    while co.active:
        f(co.returnvalue)
        co.resume
    co.returnvalue

// builtin for() made useful as a coroutine.
def cofor(n, f):
    for n: f(_)
    n

// error checking

def fatal(msg):
    if msg:
        print "fatal error: " + msg
        return from program

def check(val, msg):
    if !val: fatal(msg)
    assert val  // FIXME: needed to make return type not-nil.
