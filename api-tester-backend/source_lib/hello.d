import std.stdio;


// If you want to call a library from bun, it is important to use extern (C)
// so that the D compiler uses the C linkage.

extern (C) void say_hello()
{
	writeln("Hello, World from D!");
}

extern (C) int square(int x)
{
	return(x*x);
}
