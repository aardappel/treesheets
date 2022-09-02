// standard functions that build on top of the builtin functions useful for almost all programs

def map(xs, fun):
    let r = [].vector_capacity(xs.length)
    for(xs) x, i:
        r.push(fun(x, i))
    return r

def map2(xs, ys, fun): return map xs.length: fun(xs[_], ys[_])

def filter(xs, fun):
    let r = []
    for(xs) x, i:
        if fun(x, i):
            r.push(x)
    return r

def filter_indices(xs, fun):
    let r = []
    for(xs) x, i:
        if fun(x, i):
            r.push(i)
    return r

def partition(xs, fun):
    let t = []
    let f = []
    for(xs) x, i:
        if fun(x, i):
            t.push(x)
        else:
            f.push(x)
    return t, f

def partition_indices(xs, fun):
    let t = []
    let f = []
    for(xs) x, i:
        if fun(x, i):
            t.push(i)
        else:
            f.push(i)
    return t, f

def exists(xs, fun):
    for(xs) x, i:
        if fun(x, i):
            return true
    return false

def forever(fun): while true: fun()

def fold(xs, acc, fun):
    for xs: acc = fun(acc, _)
    return acc

def fold2(xs, acc1, acc2, fun):
    for xs:
        acc1, acc2 = fun(acc1, acc2, _)
    return acc1, acc2

def fold_reverse(xs, acc, fun):
    for xs.length: acc = fun(acc, xs[xs.length - 1 - _])
    return acc

def reduce(xs, fun):
    assert xs.length
    var acc = xs[0]
    for(xs.length - 1) i: acc = fun(acc, xs[i + 1])
    return acc

def connect(xs, fun):
    return map(max(0, xs.length - 1)) i: fun(xs[i], xs[i + 1])

def reduce_reverse(xs, fun):
    assert xs.length
    var acc = xs[xs.length - 1]
    for(xs.length - 1) i: acc = fun(xs[xs.length - i - 2], acc)
    return acc

def find(xs, fun):
    for(xs) x, i: if fun(x): return i
    return -1

// return element index for which fun returns biggest value of type R (float or int).
def find_best<R>(xs, fun):
    let init = 0
    var best:R = init
    var i = -1
    for(xs) x, j:
        let v = fun(x)
        if i < 0 or v > best:
            i = j
            best = v
    return i

// Given a function that produces a weight (float or int, specified as R) for each list element,
// return a random pick that takes that weight into account.
def weighted_pick<R, T>(list:[T], wf):
    let init = 0
    let weights = fold<[T], R>(list, init): _a + wf(_x)
    assert(weights)
    var acc:R = init
    let pick = if acc is float: rnd_float() * weights else: rnd(weights)  // compile-time if!
    for(list) x:
        acc += wf(x)
        if pick < acc: return x
    return list.top()  // just in case float precision causes pick == weights.

// These work on int or float vectors.
def sum<T>(xs:[T]):
    let init = 0
    return fold<[T], T>(xs, init): _x + _y
def product<T>(xs:[T]):
    let init = 1
    return fold<[T], T>(xs, init): _x * _y

def zip(xs, ys): return map xs.length: [ xs[_], ys[_] ]

def reverse(xs, fun): for(xs.length) i:
    let j = xs.length - i - 1
    fun(xs[j], j)

def reverse_map(xs, fun): return map(xs.length) i:
    let j = xs.length - i - 1
    fun(xs[j], j)

def reverse_list(xs): return map(xs.length) i: xs[xs.length - i - 1]

def flatten(xs): return fold(xs, []): append(_a, _b)

def split(l, f):
    let r = [ [], [] ]
    for(l) e: r[f(e)].push(e)
    return r

def rnd_pick(xs): return xs[rnd(xs.length)]
def rnd_pick2(xs): return rnd_pick(rnd_pick(xs))

def qsort<T>(xs:[T], lt) -> [T]:
    if xs.length <= 1:
        return xs
    else:
        let pivot = xs[0]
        let tail = xs.slice(1, -1)
        let f1, f2 = tail.partition(): lt(_, pivot)
        return append(append(qsort(f1, lt), [ pivot ]),
                             qsort(f2, lt))

def qsort_in_place(xs, lt):
    def rec(s, e) -> void:
        let l = e - s
        if l > 1:
            let pivot = xs[s]
            var sp = s + 1
            var ep = e
            while sp < ep:
                let c = xs[sp]
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
            var j = i
            while j > 0 and lt(key, xs[j - 1]):
                xs[j--] = xs[j - 1]
            xs[j] = key

def insert_ordered(xs, x, lt):
    for(xs) key, i:
        if lt(x, key):
            xs.insert(i, x)
            return
    xs.push(x)

def shuffle(xs):
    for(xs) x, i:
        if i:
            let j = rnd(i + 1)
            xs[i] = xs[j]
            xs[j] = x
    return xs

def nest_if(c, nest, with):
    if c: nest(with)
    else: with()

def return_after(v, f):
    f()
    return v

def do(f): return f()  // Useful to create a scope where there is none.

def for_bias (num, bias,  fun): for num: fun(_ + bias)
def for_scale(num, scale, fun): for num: fun(_ * scale)

def for_range     (a, b, fun): for_bias(b - a,     a, fun)
def for_range_incl(a, b, fun): for_bias(b - a + 1, a, fun)

// HOFs that work on other HOFs:

def collect(hof):
    let list = []
    hof(): list.push(_)
    return list

// error checking

def fatal_exit(msg):
    print "fatal error: {msg}"
    return from program

def fatal(msg):
    if msg: fatal_exit(msg)

def check(val, msg):
    if not val: fatal_exit(msg)
    else: return val

// print helpers

def print(a, b):
    print "{a} {b}"
