// These types have special status in the language, as they can be used with
// mathematical operators, and many builtin functions.

// This file is automatically included in each Lobster program (!)

// vector types: generally for float or int elements, but useful for other types as well.

struct vec2<T>:
    x:T
    y:T

struct vec3<T> : vec2<T>
    z:T

struct vec4<T> : vec3<T>
    w:T

// Specialized type names:

struct float2 = vec2<float>
struct int2 = vec2<int>

struct float3 = vec3<float>
struct int3 = vec3<int>

struct float4 = vec4<float>
struct int4 = vec4<int>

// Booleans are an enum.

enum bool:
    false
    true
