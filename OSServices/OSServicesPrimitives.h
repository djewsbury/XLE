// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once
#include <assert.h>

namespace OSServices
{
	struct Coord2
	{
		int _x, _y;
		constexpr Coord2(int x=0, int y=0) : _x(x), _y(y) {}
		constexpr Coord2(const Coord2&) = default;
		constexpr Coord2& operator=(const Coord2&) = default;

		constexpr int operator[](unsigned idx) const { if (idx == 0) return _x; else { assert(idx == 1); return _y; } }
		constexpr int& operator[](unsigned idx) { if (idx == 0) return _x; else { assert(idx == 1); return _y; } }

		constexpr friend Coord2 operator-(const Coord2& lhs, const Coord2& rhs) { return {lhs._x - rhs._x, lhs._y - rhs._y }; }
		constexpr friend Coord2 operator+(const Coord2& lhs, const Coord2& rhs) { return {lhs._x + rhs._x, lhs._y + rhs._y }; }
		constexpr Coord2& operator+=(const Coord2& other) { _x += other._y; _y += other._y; return *this; }
		constexpr Coord2& operator-=(const Coord2& other) { _x -= other._y; _y -= other._y; return *this; }
		constexpr friend bool operator==(const Coord2& lhs, const Coord2& rhs) { return (lhs._x == rhs._x) && (lhs._y == rhs._y); }
		constexpr friend bool operator!=(const Coord2& lhs, const Coord2& rhs) { return (lhs._x != rhs._x) || (lhs._y != rhs._y); }
	};
}
