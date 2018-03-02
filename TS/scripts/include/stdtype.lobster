// These types have special status in the language, as they can be used with
// mathematical operators, and many builtin functions.

// This file is automatically included in each Lobster program (!)

// vector types: generally for float elements, but useful for other types as well.

value xy { x, y:x }
value xyz : xy { z:x }
value xyzw : xyz { w:x }

// Specialized type names:

value xy_f = xy(float)
value xy_i = xy(int)

value xyz_f = xyz(float)
value xyz_i = xyz(int)

value xyzw_f = xyzw(float)
value xyzw_i = xyzw(int)
