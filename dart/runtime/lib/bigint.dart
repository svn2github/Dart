// Copyright (c) 2014, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

// Copyright 2009 The Go Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

/*
 * Copyright (c) 2003-2005  Tom Wu
 * Copyright (c) 2012 Adam Singer (adam@solvr.io)
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS-IS" AND WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS, IMPLIED OR OTHERWISE, INCLUDING WITHOUT LIMITATION, ANY
 * WARRANTY OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
 *
 * IN NO EVENT SHALL TOM WU BE LIABLE FOR ANY SPECIAL, INCIDENTAL,
 * INDIRECT OR CONSEQUENTIAL DAMAGES OF ANY KIND, OR ANY DAMAGES WHATSOEVER
 * RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER OR NOT ADVISED OF
 * THE POSSIBILITY OF DAMAGE, AND ON ANY THEORY OF LIABILITY, ARISING OUT
 * OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * In addition, the following condition applies:
 *
 * All redistributions must retain an intact copy of this copyright notice
 * and disclaimer.
 */

class _Bigint extends _IntegerImplementation implements int {
  // Bits per digit.
  static const int DIGIT_BITS = 32;
  static const int DIGIT_BASE = 1 << DIGIT_BITS;
  static const int DIGIT_MASK = (1 << DIGIT_BITS) - 1;

  // Bits per half digit.
  static const int DIGIT2_BITS = DIGIT_BITS >> 1;
  static const int DIGIT2_BASE = 1 << DIGIT2_BITS;
  static const int DIGIT2_MASK = (1 << DIGIT2_BITS) - 1;

  // Allocate extra digits so the bigint can be reused.
  static const int EXTRA_DIGITS = 4;

  // Floating-point unit integer precision.
  static const int FP_BITS = 52;
  static const int FP_BASE = 1 << FP_BITS;
  static const int FP_D1 = FP_BITS - DIGIT_BITS;
  static const int FP_D2 = 2 * DIGIT_BITS - FP_BITS;

  // Min and max of non bigint values.
  static const int MIN_INT64 = (-1) << 63;
  static const int MAX_INT64 = 0x7fffffffffffffff;

  // Bigint constant values.
  // Note: Not declared as final in order to satisfy optimizer, which expects
  // constants to be in canonical form (Smi).
  static _Bigint ZERO = new _Bigint();
  static _Bigint ONE = new _Bigint()._setInt(1);

  // Digit conversion table for parsing.
  static final Map<int, int> DIGIT_TABLE = _createDigitTable();

  // Internal data structure.
  bool get _neg native "Bigint_getNeg";
  void set _neg(bool neg) native "Bigint_setNeg";
  int get _used native "Bigint_getUsed";
  void set _used(int used) native "Bigint_setUsed";
  Uint32List get _digits native "Bigint_getDigits";
  void set _digits(Uint32List digits) native "Bigint_setDigits";

  // Factory returning an instance initialized to value 0.
  factory _Bigint() native "Bigint_allocate";

  // Factory returning an instance initialized to an integer value.
  factory _Bigint._fromInt(int i) {
    return new _Bigint()._setInt(i);
  }

  // Factory returning an instance initialized to a hex string.
  factory _Bigint._fromHex(String s) {
    return new _Bigint()._setHex(s);
  }

  // Factory returning an instance initialized to a double value given by its
  // components.
  factory _Bigint._fromDouble(int sign, int significand, int exponent) {
    return new _Bigint()._setDouble(sign, significand, exponent);
  }

  // Initialize instance to the given value no larger than a Mint.
  _Bigint _setInt(int i) {
    assert(i is! _Bigint);
    _ensureLength(2);
    _used = 2;
    var l, h;
    if (i < 0) {
      _neg = true;
      if (i == MIN_INT64) {
        l = 0;
        h = 0x80000000;
      } else {
        l = (-i) & DIGIT_MASK;
        h = (-i) >> DIGIT_BITS;
      }
    } else {
      _neg = false;
      l = i & DIGIT_MASK;
      h = i >> DIGIT_BITS;
    }
    _digits[0] = l;
    _digits[1] = h;
    _clamp();
    return this;
  }

  // Initialize instance to the given hex string.
  // TODO(regis): Copy Bigint::NewFromHexCString, fewer digit accesses.
  // TODO(regis): Unused.
  _Bigint _setHex(String s) {
    const int HEX_BITS = 4;
    const int HEX_DIGITS_PER_DIGIT = 8;
    var hexDigitIndex = s.length;
    _ensureLength((hexDigitIndex + HEX_DIGITS_PER_DIGIT - 1) ~/ HEX_DIGITS_PER_DIGIT);
    var bitIndex = 0;
    while (--hexDigitIndex >= 0) {
      var digit = DIGIT_TABLE[s.codeUnitAt(hexDigitIndex)];
      if (digit = null) {
        if (s[hexDigitIndex] == "-") _neg = true;
        continue;  // Ignore invalid digits.
      }
      _neg = false;  // Ignore "-" if not at index 0.
      if (bitIndex == 0) {
        _digits[_used++] = digit;
        // TODO(regis): What if too many bad digits were ignored and
        // _used becomes larger than _digits.length? error or reallocate?
      } else {
        _digits[_used - 1] |= digit << bitIndex;
      }
      bitIndex = (bitIndex + HEX_BITS) % DIGIT_BITS;
    }
    _clamp();
    return this;
  }

  // Initialize instance to the given double value.
  _Bigint _setDouble(int sign, int significand, int exponent) {
    assert(significand >= 0);
    assert(exponent >= 0);
    _setInt(significand);
    _neg = sign < 0;
    if (exponent > 0) {
      _lShiftTo(exponent, this);
    }
    return this;
  }

  // Create digit conversion table for parsing.
  static Map<int, int> _createDigitTable() {
    Map table = new HashMap();
    int digit, value;
    digit = "0".codeUnitAt(0);
    for(value = 0; value <= 9; ++value) table[digit++] = value;
    digit = "a".codeUnitAt(0);
    for(value = 10; value < 36; ++value) table[digit++] = value;
    digit = "A".codeUnitAt(0);
    for(value = 10; value < 36; ++value) table[digit++] = value;
    return table;
  }

  // Return most compact integer (i.e. possibly Smi or Mint).
  // TODO(regis): Intrinsify.
  int _toValidInt() {
    assert(DIGIT_BITS == 32);  // Otherwise this code needs to be revised.
    if (_used == 0) return 0;
    if (_used == 1) return _neg ? -_digits[0] : _digits[0];
    if (_used > 2) return this;
    if (_neg) {
      if (_digits[1] > 0x80000000) return this;
      if (_digits[1] == 0x80000000) {
        if (_digits[0] > 0) return this;
        return MIN_INT64;
      }
      return -((_digits[1] << DIGIT_BITS) | _digits[0]);
    }
    if (_digits[1] >= 0x80000000) return this;
    return (_digits[1] << DIGIT_BITS) | _digits[0];
  }

  // Conversion from int to bigint.
  _Bigint _toBigint() => this;

  // Make sure at least 'length' _digits are allocated.
  // Copy existing _digits if reallocation is necessary.
  // TODO(regis): Check that we are not preserving _digits unnecessarily.
  void _ensureLength(int length) {
    if (length > 0 && (_digits == null || length > _digits.length)) {
      var new_digits = new Uint32List(length + EXTRA_DIGITS);
      if (_digits != null) {
        for (var i = _used; --i >= 0; ) {
          new_digits[i] = _digits[i];
        }
      }
      _digits = new_digits;
    }
  }

  // Clamp off excess high _digits.
  void _clamp() {
    while (_used > 0 && _digits[_used - 1] == 0) {
      --_used;
    }
    assert(_used > 0 || !_neg);
  }

  // Copy this to r.
  void _copyTo(_Bigint r) {
    r._ensureLength(_used);
    for (var i = _used - 1; i >= 0; --i) {
      r._digits[i] = _digits[i];
    }
    r._used = _used;
    r._neg = _neg;
  }

  // Return the bit length of digit x.
  int _nbits(int x) {
    var r = 1, t;
    if ((t = x >> 16) != 0) { x = t; r += 16; }
    if ((t = x >> 8) != 0) { x = t; r += 8; }
    if ((t = x >> 4) != 0) { x = t; r += 4; }
    if ((t = x >> 2) != 0) { x = t; r += 2; }
    if ((x >> 1) != 0) { r += 1; }
    return r;
  }

  // r = this << n*DIGIT_BITS.
  void _dlShiftTo(int n, _Bigint r) {
    var r_used = _used + n;
    r._ensureLength(r_used);
    for (var i = _used - 1; i >= 0; --i) {
      r._digits[i + n] = _digits[i];
    }
    for (var i = n - 1; i >= 0; --i) {
      r._digits[i] = 0;
    }
    r._used = r_used;
    r._neg = _neg;
  }

  // r = this >> n*DIGIT_BITS.
  void _drShiftTo(int n, _Bigint r) {
    var r_used = _used - n;
    if (r_used < 0) {
      if (_neg) {
        // Set r to -1.
        r._neg = true;
        r._ensureLength(1);
        r._used = 1;
        r._digits[0] = 1;
      } else {
        // Set r to 0.
        r._neg = false;
        r._used = 0;
      }
      return;
    }
    r._ensureLength(r_used);
    for (var i = n; i < _used; ++i) {
      r._digits[i - n] = _digits[i];
    }
    r._used = r_used;
    r._neg = _neg;
    if (_neg) {
      // Round down if any bit was shifted out.
      for (var i = 0; i < n; i++) {
        if (_digits[i] != 0) {
          r._subTo(ONE, r);
          break;
        }
      }
    }
  }

  // r = this << n.
  void _lShiftTo(int n, _Bigint r) {
    var ds = n ~/ DIGIT_BITS;
    var bs = n % DIGIT_BITS;
    if (bs == 0) {
      _dlShiftTo(ds, r);
      return;
    }
    var cbs = DIGIT_BITS - bs;
    var bm = (1 << cbs) - 1;
    var r_used = _used + ds + 1;
    r._ensureLength(r_used);
    var c = 0;
    for (var i = _used - 1; i >= 0; --i) {
      r._digits[i + ds + 1] = (_digits[i] >> cbs) | c;
      c = (_digits[i] & bm) << bs;
    }
    for (var i = ds - 1; i >= 0; --i) {
      r._digits[i] = 0;
    }
    r._digits[ds] = c;
    r._used = r_used;
    r._neg = _neg;
    r._clamp();
  }

  // r = this >> n.
  void _rShiftTo(int n, _Bigint r) {
    var ds = n ~/ DIGIT_BITS;
    var bs = n % DIGIT_BITS;
    if (bs == 0) {
      _drShiftTo(ds, r);
      return;
    }
    var r_used = _used - ds;
    if (r_used <= 0) {
      if (_neg) {
        // Set r to -1.
        r._neg = true;
        r._ensureLength(1);
        r._used = 1;
        r._digits[0] = 1;
      } else {
        // Set r to 0.
        r._neg = false;
        r._used = 0;
      }
      return;
    }
    var cbs = DIGIT_BITS - bs;
    var bm = (1 << bs) - 1;
    r._ensureLength(r_used);
    r._digits[0] = _digits[ds] >> bs;
    for (var i = ds + 1; i < _used; ++i) {
      r._digits[i - ds - 1] |= (_digits[i] & bm) << cbs;
      r._digits[i - ds] = _digits[i] >> bs;
    }
    r._neg = _neg;
    r._used = r_used;
    r._clamp();
    if (_neg) {
      // Round down if any bit was shifted out.
      if ((_digits[ds] & bm) != 0) {
        r._subTo(ONE, r);
        return;
      }
      for (var i = 0; i < ds; i++) {
        if (_digits[i] != 0) {
          r._subTo(ONE, r);
          return;
        }
      }
    }
  }

  // Return 0 if abs(this) == abs(a).
  // Return a positive number if abs(this) > abs(a).
  // Return a negative number if abs(this) < abs(a).
  int _absCompareTo(_Bigint a) {
    var r = _used - a._used;
    if (r == 0) {
      var i = _used;
      while (--i >= 0 && (r = _digits[i] - a._digits[i]) == 0);
    }
    return r;
  }

  // Return 0 if this == a.
  // Return a positive number if this > a.
  // Return a negative number if this < a.
  int _compareTo(_Bigint a) {
    var r;
    if (_neg == a._neg) {
      r = _absCompareTo(a);
      if (_neg) {
        r = -r;
      }
    } else if (_neg) {
      r = -1;
    } else {
      r = 1;
    }
    return r;
  }

  // r = abs(this) + abs(a).
  void _absAddTo(_Bigint a, _Bigint r) {
    if (_used < a._used) {
      a._absAddTo(this, r);
      return;
    }
    if (_used == 0) {
      // Set r to 0.
      r._neg = false;
      r._used = 0;
      return;
    }
    if (a._used == 0) {
      _copyTo(r);
      return;
    }
    r._ensureLength(_used + 1);
    var c = 0;
    for (var i = 0; i < a._used; i++) {
      c += _digits[i] + a._digits[i];
      r._digits[i] = c & DIGIT_MASK;
      c >>= DIGIT_BITS;
    }
    for (var i = a._used; i < _used; i++) {
      c += _digits[i];
      r._digits[i] = c & DIGIT_MASK;
      c >>= DIGIT_BITS;
    }
    r._digits[_used] = c;
    r._used = _used + 1;
    r._clamp();
  }

  // r = abs(this) - abs(a), with abs(this) >= abs(a).
  void _absSubTo(_Bigint a, _Bigint r) {
    assert(_absCompareTo(a) >= 0);
    if (_used == 0) {
      // Set r to 0.
      r._neg = false;
      r._used = 0;
      return;
    }
    if (a._used == 0) {
      _copyTo(r);
      return;
    }
    r._ensureLength(_used);
    var c = 0;
    for (var i = 0; i < a._used; i++) {
      c += _digits[i] - a._digits[i];
      r._digits[i] = c & DIGIT_MASK;
      c >>= DIGIT_BITS;
    }
    for (var i = a._used; i < _used; i++) {
      c += _digits[i];
      r._digits[i] = c & DIGIT_MASK;
      c >>= DIGIT_BITS;
    }
    r._used = _used;
    r._clamp();
  }

  // r = abs(this) & abs(a).
  void _absAndTo(_Bigint a, _Bigint r) {
    var r_used = (_used < a._used) ? _used : a._used;
    r._ensureLength(r_used);
    for (var i = 0; i < r_used; i++) {
      r._digits[i] = _digits[i] & a._digits[i];
    }
    r._used = r_used;
    r._clamp();
  }

  // r = abs(this) &~ abs(a).
  void _absAndNotTo(_Bigint a, _Bigint r) {
    var r_used = _used;
    r._ensureLength(r_used);
    var m = (r_used < a._used) ? r_used : a._used;
    for (var i = 0; i < m; i++) {
      r._digits[i] = _digits[i] &~ a._digits[i];
    }
    for (var i = m; i < r_used; i++) {
      r._digits[i] = _digits[i];
    }
    r._used = r_used;
    r._clamp();
  }

  // r = abs(this) | abs(a).
  void _absOrTo(_Bigint a, _Bigint r) {
    var r_used = (_used > a._used) ? _used : a._used;
    r._ensureLength(r_used);
    var l, m;
    if (_used < a._used) {
      l = a;
      m = _used;
    } else {
      l = this;
      m = a._used;
    }
    for (var i = 0; i < m; i++) {
      r._digits[i] = _digits[i] | a._digits[i];
    }
    for (var i = m; i < r_used; i++) {
      r._digits[i] = l._digits[i];
    }
    r._used = r_used;
    r._clamp();
  }

  // r = abs(this) ^ abs(a).
  void _absXorTo(_Bigint a, _Bigint r) {
    var r_used = (_used > a._used) ? _used : a._used;
    r._ensureLength(r_used);
    var l, m;
    if (_used < a._used) {
      l = a;
      m = _used;
    } else {
      l = this;
      m = a._used;
    }
    for (var i = 0; i < m; i++) {
      r._digits[i] = _digits[i] ^ a._digits[i];
    }
    for (var i = m; i < r_used; i++) {
      r._digits[i] = l._digits[i];
    }
    r._used = r_used;
    r._clamp();
  }

  // Return r = this & a.
  _Bigint _andTo(_Bigint a, _Bigint r) {
    if (_neg == a._neg) {
      if (_neg) {
        // (-this) & (-a) == ~(this-1) & ~(a-1)
        //                == ~((this-1) | (a-1))
        //                == -(((this-1) | (a-1)) + 1)
        _Bigint t1 = new _Bigint();
        _absSubTo(ONE, t1);
        _Bigint a1 = new _Bigint();
        a._absSubTo(ONE, a1);
        t1._absOrTo(a1, r);
        r._absAddTo(ONE, r);
        r._neg = true;  // r cannot be zero if this and a are negative.
        return r;
      }
      _absAndTo(a, r);
      r._neg = false;
      return r;
    }
    // _neg != a._neg
    var p, n;
    if (_neg) {
      p = a;
      n = this;
    } else {  // & is symmetric.
      p = this;
      n = a;
    }
    // p & (-n) == p & ~(n-1) == p &~ (n-1)
    _Bigint n1 = new _Bigint();
    n._absSubTo(ONE, n1);
    p._absAndNotTo(n1, r);
    r._neg = false;
    return r;
  }

  // Return r = this &~ a.
  _Bigint _andNotTo(_Bigint a, _Bigint r) {
    if (_neg == a._neg) {
      if (_neg) {
        // (-this) &~ (-a) == ~(this-1) &~ ~(a-1)
        //                 == ~(this-1) & (a-1)
        //                 == (a-1) &~ (this-1)
        _Bigint t1 = new _Bigint();
        _absSubTo(ONE, t1);
        _Bigint a1 = new _Bigint();
        a._absSubTo(ONE, a1);
        a1._absAndNotTo(t1, r);
        r._neg = false;
        return r;
      }
      _absAndNotTo(a, r);
      r._neg = false;
      return r;
    }
    if (_neg) {
      // (-this) &~ a == ~(this-1) &~ a
      //              == ~(this-1) & ~a
      //              == ~((this-1) | a)
      //              == -(((this-1) | a) + 1)
      _Bigint t1 = new _Bigint();
      _absSubTo(ONE, t1);
      t1._absOrTo(a, r);
      r._absAddTo(ONE, r);
      r._neg = true;  // r cannot be zero if this is negative and a is positive.
      return r;
    }
    // this &~ (-a) == this &~ ~(a-1) == this & (a-1)
    _Bigint a1 = new _Bigint();
    a._absSubTo(ONE, a1);
    _absAndTo(a1, r);
    r._neg = false;
    return r;
  }

  // Return r = this | a.
  _Bigint _orTo(_Bigint a, _Bigint r) {
    if (_neg == a._neg) {
      if (_neg) {
        // (-this) | (-a) == ~(this-1) | ~(a-1)
        //                == ~((this-1) & (a-1))
        //                == -(((this-1) & (a-1)) + 1)
        _Bigint t1 = new _Bigint();
        _absSubTo(ONE, t1);
        _Bigint a1 = new _Bigint();
        a._absSubTo(ONE, a1);
        t1._absAndTo(a1, r);
        r._absAddTo(ONE, r);
        r._neg = true;  // r cannot be zero if this and a are negative.
        return r;
      }
      _absOrTo(a, r);
      r._neg = false;
      return r;
    }
    // _neg != a._neg
    var p, n;
    if (_neg) {
      p = a;
      n = this;
    } else {  // | is symmetric.
      p = this;
      n = a;
    }
    // p | (-n) == p | ~(n-1) == ~((n-1) &~ p) == -(~((n-1) &~ p) + 1)
    _Bigint n1 = new _Bigint();
    n._absSubTo(ONE, n1);
    n1._absAndNotTo(p, r);
    r._absAddTo(ONE, r);
    r._neg = true;  // r cannot be zero if only one of this or a is negative.
    return r;
  }

  // Return r = this ^ a.
  _Bigint _xorTo(_Bigint a, _Bigint r) {
    if (_neg == a._neg) {
      if (_neg) {
        // (-this) ^ (-a) == ~(this-1) ^ ~(a-1) == (this-1) ^ (a-1)
        _Bigint t1 = new _Bigint();
        _absSubTo(ONE, t1);
        _Bigint a1 = new _Bigint();
        a._absSubTo(ONE, a1);
        t1._absXorTo(a1, r);
        r._neg = false;
        return r;
      }
      _absXorTo(a, r);
      r._neg = false;
      return r;
    }
    // _neg != a._neg
    var p, n;
    if (_neg) {
      p = a;
      n = this;
    } else {  // ^ is symmetric.
      p = this;
      n = a;
    }
    // p ^ (-n) == p ^ ~(n-1) == ~(p ^ (n-1)) == -((p ^ (n-1)) + 1)
    _Bigint n1 = new _Bigint();
    n._absSubTo(ONE, n1);
    p._absXorTo(n1, r);
    r._absAddTo(ONE, r);
    r._neg = true;  // r cannot be zero if only one of this or a is negative.
    return r;
  }

  // Return r = ~this.
  _Bigint _notTo(_Bigint r) {
    if (_neg) {
      // ~(-this) == ~(~(this-1)) == this-1
      _absSubTo(ONE, r);
      r._neg = false;
      return r;
    }
    // ~this == -this-1 == -(this+1)
    _absAddTo(ONE, r);
    r._neg = true;  // r cannot be zero if this is positive.
    return r;
  }

  // Return r = this + a.
  _Bigint _addTo(_Bigint a, _Bigint r) {
    var r_neg = _neg;
    if (_neg == a._neg) {
      // this + a == this + a
      // (-this) + (-a) == -(this + a)
      _absAddTo(a, r);
    } else {
      // this + (-a) == this - a == -(this - a)
      // (-this) + a == a - this == -(this - a)
      if (_absCompareTo(a) >= 0) {
        _absSubTo(a, r);
      } else {
        r_neg = !r_neg;
        a._absSubTo(this, r);
      }
    }
  	r._neg = r_neg;
    return r;
  }

  // Return r = this - a.
  _Bigint _subTo(_Bigint a, _Bigint r) {
  	var r_neg = _neg;
    if (_neg != a._neg) {
  		// this - (-a) == this + a
  		// (-this) - a == -(this + a)
      _absAddTo(a, r);
  	} else {
  		// this - a == this - a == -(this - a)
  		// (-this) - (-a) == a - this == -(this - a)
      if (_absCompareTo(a) >= 0) {
        _absSubTo(a, r);
  		} else {
        r_neg = !r_neg;
        a._absSubTo(this, r);
      }
    }
  	r._neg = r_neg;
    return r;
  }

  // Accumulate multiply.
  // this[i..i+n-1]: bigint multiplicand.
  // x: digit multiplier, 0 <= x < DIGIT_BASE (i.e. 32-bit multiplier).
  // w[j..j+n-1]: bigint accumulator.
  // Returns carry out.
  // w[j..j+n-1] += this[i..i+n-1] * x.
  // Returns carry out.
  int _am(int i, int x, _Bigint w, int j, int n) {
    if (x == 0) {
      // No-op if x is 0.
      return 0;
    }
    int c = 0;
    int xl = x & DIGIT2_MASK;
    int xh = x >> DIGIT2_BITS;
    while (--n >= 0) {
      int l = _digits[i] & DIGIT2_MASK;
      int h = _digits[i++] >> DIGIT2_BITS;
      int m = xh*l + h*xl;
      l = xl*l + ((m & DIGIT2_MASK) << DIGIT2_BITS) + w._digits[j] + c;
      c = (l >> DIGIT_BITS) + (m >> DIGIT2_BITS) + xh*h;
      w._digits[j++] = l & DIGIT_MASK;
    }
    return c;
  }

  // Accumulate multiply with carry.
  // this[i..i+n-1]: bigint multiplicand.
  // x: digit multiplier, 0 <= x < 2*DIGIT_BASE  (i.e. 33-bit multiplier).
  // w[j..j+n-1]: bigint accumulator.
  // c: int carry in.
  // Returns carry out.
  // w[j..j+n-1] += this[i..i+n-1] * x + c.
  // Returns carry out.
  int _amc(int i, int x, _Bigint w, int j, int c, int n) {
    if (x == 0 && c == 0) {
      // No-op if both x and c are 0.
      return 0;
    }
    int xl = x & DIGIT2_MASK;
    int xh = x >> DIGIT2_BITS;
    while (--n >= 0) {
      int l = _digits[i] & DIGIT2_MASK;
      int h = _digits[i++] >> DIGIT2_BITS;
      int m = xh*l + h*xl;
      l = xl*l + ((m & DIGIT2_MASK) << DIGIT2_BITS) + w._digits[j] + c;
      c = (l >> DIGIT_BITS) + (m >> DIGIT2_BITS) + xh*h;
      w._digits[j++] = l & DIGIT_MASK;
    }
    return c;
  }

  // r = this * a.
  void _mulTo(_Bigint a, _Bigint r) {
    // TODO(regis): Use karatsuba multiplication when appropriate.
    var i = _used;
    r._ensureLength(i + a._used);
    r._used = i + a._used;
    while (--i >= 0) {
      r._digits[i] = 0;
    }
    for (i = 0; i < a._used; ++i) {
      r._digits[i + _used] = _am(0, a._digits[i], r, i, _used);
    }
    r._clamp();
    r._neg = r._used > 0 && _neg != a._neg;  // Zero cannot be negative.
  }

  // r = this^2, r != this.
  void _sqrTo(_Bigint r) {
    var i = 2 * _used;
    r._ensureLength(i);
    r._used = i;
    while (--i >= 0) {
      r._digits[i] = 0;
    }
    for (i = 0; i < _used - 1; ++i) {
      var c = _am(i, _digits[i], r, 2*i, 1);
      var d = r._digits[i + _used];
      d += _amc(i + 1, _digits[i] << 1, r, 2*i + 1, c, _used - i - 1);
      if (d >= DIGIT_BASE) {
        r._digits[i + _used] = d - DIGIT_BASE;
        r._digits[i + _used + 1] = 1;
      } else {
        r._digits[i + _used] = d;
      }
    }
    if (r._used > 0) {
      r._digits[r._used - 1] += _am(i, _digits[i], r, 2*i, 1);
    }
    r._neg = false;
    r._clamp();
  }

  // Truncating division and remainder.
  // If q != null, q = trunc(this / a).
  // If r != null, r = this - a * trunc(this / a).
  void _divRemTo(_Bigint a, _Bigint q, _Bigint r) {
    if (a._used == 0) return;
    if (_used < a._used) {
      if (q != null) {
        // Set q to 0.
        q._neg = false;
        q._used = 0;
      }
      if (r != null) {
        _copyTo(r);
      }
      return;
    }
    if (r == null) {
      r = new _Bigint();
    }
    var y = new _Bigint();
    var nsh = DIGIT_BITS - _nbits(a._digits[a._used - 1]);  // normalize modulus
    if (nsh > 0) {
      a._lShiftTo(nsh, y);
      _lShiftTo(nsh, r);
    }
    else {
      a._copyTo(y);
      _copyTo(r);
    }
    // We consider this and a positive. Ignore the copied sign.
    y._neg = false;
    r._neg = false;
    var y_used = y._used;
    var y0 = y._digits[y_used - 1];
    if (y0 == 0) return;
    var yt = y0*(1 << FP_D1) + ((y_used > 1) ? y._digits[y_used - 2] >> FP_D2 : 0);
    var d1 = FP_BASE/yt;
    var d2 = (1 << FP_D1)/yt;
    var e = 1 << FP_D2;
    var i = r._used;
    var j = i - y_used;
    _Bigint t = (q == null) ? new _Bigint() : q;

    y._dlShiftTo(j, t);

    if (r._compareTo(t) >= 0) {
      r._digits[r._used++] = 1;
      r._subTo(t, r);
    }
    ONE._dlShiftTo(y_used, t);
    t._subTo(y, y);  // "negative" y so we can replace sub with _am later
    while (y._used < y_used) {
      y._digits[y._used++] = 0;
    }
    while (--j >= 0) {
      // Estimate quotient digit
      var qd = (r._digits[--i] == y0)
          ? DIGIT_MASK
          : (r._digits[i]*d1 + (r._digits[i - 1] + e)*d2).floor();
      if ((r._digits[i] += y._amc(0, qd, r, j, 0, y_used)) < qd) {  // Try it out
        y._dlShiftTo(j, t);
        r._subTo(t, r);
        while (r._digits[i] < --qd) {
          r._subTo(t, r);
        }
      }
    }
    if (q != null) {
      r._drShiftTo(y_used, q);
      if (_neg != a._neg) {
        ZERO._subTo(q, q);
      }
    }
    r._used = y_used;
    r._clamp();
    if (nsh > 0) {
      r._rShiftTo(nsh, r);  // Denormalize remainder
    }
    if (_neg) {
      ZERO._subTo(r, r);
    }
  }

  int get _identityHashCode {
    return this;
  }
  int operator ~() {
    _Bigint result = new _Bigint();
    _notTo(result);
    return result._toValidInt();
  }

  int get bitLength {
    if (_used == 0) return 0;
    if (_neg) return (~this).bitLength;
    return DIGIT_BITS*(_used - 1) + _nbits(_digits[_used - 1]);
  }

  // This method must support smi._toBigint()._shrFromInt(int).
  int _shrFromInt(int other) {
    if (_used == 0) return other;  // Shift amount is zero.
    if (_neg) throw "negative shift amount";  // TODO(regis): What exception?
    assert(DIGIT_BITS == 32);  // Otherwise this code needs to be revised.
    var shift;
    if (_used > 2 || (_used == 2 && _digits[1] > 0x10000000)) {
      if (other < 0) {
        return -1;
      } else {
        return 0;
      }
    } else {
      shift = ((_used == 2) ? (_digits[1] << DIGIT_BITS) : 0) + _digits[0];
    }
    _Bigint result = new _Bigint();
    other._toBigint()._rShiftTo(shift, result);
    return result._toValidInt();
  }

  // This method must support smi._toBigint()._shlFromInt(int).
  // An out of memory exception is thrown if the result cannot be allocated.
  int _shlFromInt(int other) {
    if (_used == 0) return other;  // Shift amount is zero.
    if (_neg) throw "negative shift amount";  // TODO(regis): What exception?
    assert(DIGIT_BITS == 32);  // Otherwise this code needs to be revised.
    var shift;
    if (_used > 2 || (_used == 2 && _digits[1] > 0x10000000)) {
      throw new OutOfMemoryError();
    } else {
      shift = ((_used == 2) ? (_digits[1] << DIGIT_BITS) : 0) + _digits[0];
    }
    _Bigint result = new _Bigint();
    other._toBigint()._lShiftTo(shift, result);
    return result._toValidInt();
  }

  // Overriden operators and methods.

  // The following operators override operators of _IntegerImplementation for
  // efficiency, but are not necessary for correctness. They shortcut native
  // calls that would return null because the receiver is _Bigint.
  num operator +(num other) {
    return other._toBigintOrDouble()._addFromInteger(this);
  }
  num operator -(num other) {
    return other._toBigintOrDouble()._subFromInteger(this);
  }
  num operator *(num other) {
    return other._toBigintOrDouble()._mulFromInteger(this);
  }
  num operator ~/(num other) {
    if ((other is int) && (other == 0)) {
      throw const IntegerDivisionByZeroException();
    }
    return other._toBigintOrDouble()._truncDivFromInteger(this);
  }
  num operator %(num other) {
    if ((other is int) && (other == 0)) {
      throw const IntegerDivisionByZeroException();
    }
    return other._toBigintOrDouble()._moduloFromInteger(this);
  }
  int operator &(int other) {
    return other._toBigintOrDouble()._bitAndFromInteger(this);
  }
  int operator |(int other) {
    return other._toBigintOrDouble()._bitOrFromInteger(this);
  }
  int operator ^(int other) {
    return other._toBigintOrDouble()._bitXorFromInteger(this);
  }
  int operator >>(int other) {
    return other._toBigintOrDouble()._shrFromInt(this);
  }
  int operator <<(int other) {
    return other._toBigintOrDouble()._shlFromInt(this);
  }
  // End of operator shortcuts.

  int operator -() {
    if (_used == 0) {
      return this;
    }
    var r = new _Bigint();
    _copyTo(r);
    r._neg = !_neg;
    return r._toValidInt();
  }

  int get sign {
    return (_used == 0) ? 0 : _neg ? -1 : 1;
  }

  bool get isEven => _used == 0 || (_digits[0] & 1) == 0;
  bool get isNegative => _neg;

  _leftShiftWithMask32(int count, int mask) {
    if (_used == 0) return 0;
    if (count is! _Smi) {
      _shlFromInt(count);  // Throws out of memory exception.
    }
    assert(DIGIT_BITS == 32);  // Otherwise this code needs to be revised.
    if (count > 31) return 0;
    return (_digits[0] << count) & mask;
  }

  int _bitAndFromInteger(int other) {
    _Bigint result = new _Bigint();
    other._toBigint()._andTo(this, result);
    return result._toValidInt();
  }
  int _bitOrFromInteger(int other) {
    _Bigint result = new _Bigint();
    other._toBigint()._orTo(this, result);
    return result._toValidInt();
  }
  int _bitXorFromInteger(int other) {
    _Bigint result = new _Bigint();
    other._toBigint()._xorTo(this, result);
    return result._toValidInt();
  }
  int _addFromInteger(int other) {
    _Bigint result = new _Bigint();
    other._toBigint()._addTo(this, result);
    return result._toValidInt();
  }
  int _subFromInteger(int other) {
    _Bigint result = new _Bigint();
    other._toBigint()._subTo(this, result);
    return result._toValidInt();
  }
  int _mulFromInteger(int other) {
    _Bigint result = new _Bigint();
    other._toBigint()._mulTo(this, result);
    return result._toValidInt();
  }
  int _truncDivFromInteger(int other) {
    _Bigint result = new _Bigint();
    other._toBigint()._divRemTo(this, result, null);
    return result._toValidInt();
  }
  int _moduloFromInteger(int other) {
    _Bigint result = new _Bigint();
    var ob = other._toBigint();
    other._toBigint()._divRemTo(this, null, result);
    if (result._neg) {
      if (_neg) {
        result._subTo(this, result);
      } else {
        result._addTo(this, result);
      }
    }
    return result._toValidInt();
  }
  int _remainderFromInteger(int other) {
    _Bigint result = new _Bigint();
    other._toBigint()._divRemTo(this, null, result);
    return result._toValidInt();
  }
  bool _greaterThanFromInteger(int other) {
    return other._toBigint()._compareTo(this) > 0;
  }
  bool _equalToInteger(int other) {
    return other._toBigint()._compareTo(this) == 0;
  }

  // Return -1/this % DIGIT_BASE, useful for Montgomery reduction.
  //
  //         xy == 1 (mod m)
  //         xy =  1+km
  //   xy(2-xy) = (1+km)(1-km)
  // x(y(2-xy)) = 1-k^2 m^2
  // x(y(2-xy)) == 1 (mod m^2)
  // if y is 1/x mod m, then y(2-xy) is 1/x mod m^2
  // Should reduce x and y(2-xy) by m^2 at each step to keep size bounded.
  int _invDigit() {
    if (_used == 0) return 0;
    var x = _digits[0];
    if ((x & 1) == 0) return 0;
    var y = x & 3;    // y == 1/x mod 2^2
    y = (y*(2 - (x & 0xf)*y)) & 0xf;  // y == 1/x mod 2^4
    y = (y*(2 - (x & 0xff)*y)) & 0xff;  // y == 1/x mod 2^8
    y = (y*(2 - (((x & 0xffff)*y) & 0xffff))) & 0xffff; // y == 1/x mod 2^16
    // Last step - calculate inverse mod DIGIT_BASE directly;
    // Assumes 16 < DIGIT_BITS <= 32 and assumes ability to handle 48-bit ints.
    y = (y*(2 - x*y % DIGIT_BASE)) % DIGIT_BASE;    // y == 1/x mod DIGIT_BASE
    // We really want the negative inverse, and - DIGIT_BASE < y < DIGIT_BASE.
    return (y > 0) ? DIGIT_BASE - y : -y;
  }

  // TODO(regis): Make this method private once the plumbing to invoke it from
  // dart:math is in place.
  // Return pow(this, e) % m.
  int modPow(int e, int m) {
    // TODO(regis): Where/how do we handle values of e smaller than 256?
    // TODO(regis): Where/how do we handle even values of m?
    assert(e >= 256 && !m.isEven());
    if (e is! _Bigint) {
      _Reduction z = new _Montgomery(m);
      var r = new _Bigint();
      var r2 = new _Bigint();
      var g = z._convert(this);
      int i = _nbits(e) - 1;
      g._copyTo(r);
      while (--i >= 0) {
        z._sqrTo(r, r2);
        if ((e & (1 << i)) > 0) {
          z._mulTo(r2, g, r);
        } else {
          var t = r;
          r = r2;
          r2 = t;
        }
      }
      return z._revert(r)._toValidInt();
    }
    var i = e.bitLength;
    var k;
    var r = new _Bigint()._setInt(1);
    if (i <= 0) return r;
    // TODO(regis): Are these values of k really optimal for our implementation?
    else if (i < 18) k = 1;
    else if (i < 48) k = 3;
    else if (i < 144) k = 4;
    else if (i < 768) k = 5;
    else k = 6;
    _Reduction z = new _Montgomery(m);
    var n = 3;
    var k1 = k - 1;
    var km = (1 << k) - 1;
    List g = new List(km + 1);
    g[1] = z._convert(this);
    if (k > 1) {
      var g2 = new _Bigint();
      z._sqrTo(g[1], g2);
      while (n <= km) {
        g[n] = new _Bigint();
        z._mulTo(g2, g[n - 2], g[n]);
        n += 2;
      }
    }
    var j = e._used - 1;
    var w;
    var is1 = true;
    var r2 = new _Bigint();
    var t;
    i = _nbits(e._digits[j]) - 1;
    while (j >= 0) {
      if (i >= k1) {
        w = (e._digits[j] >> (i - k1)) & km;
      } else {
        w = (e._digits[j] & ((1 << (i + 1)) - 1)) << (k1 - i);
        if (j > 0) {
          w |= e._digits[j - 1] >> (DIGIT_BITS + i - k1);
        }
      }
      n = k;
      while ((w & 1) == 0) {
        w >>= 1;
        --n;
      }
      if ((i -= n) < 0) {
        i += DIGIT_BITS;
        --j;
      }
      if (is1) {  // r == 1, don't bother squaring or multiplying it.
        g[w]._copyTo(r);
        is1 = false;
      }
      else {
        while (n > 1) {
          z._sqrTo(r, r2);
          z._sqrTo(r2, r);
          n -= 2;
        }
        if (n > 0) {
          z._sqrTo(r, r2);
        } else {
          t = r;
          r = r2;
          r2 = t;
        }
        z._mulTo(r2,g[w], r);
      }

      while (j >= 0 && (e._digits[j] & (1 << i)) == 0) {
        z._sqrTo(r, r2);
        t = r;
        r = r2;
        r2 = t;
        if (--i < 0) {
          i = DIGIT_BITS - 1;
          --j;
        }
      }
    }
    return z._revert(r)._toValidInt();
  }
}

// New classes to support crypto (modPow method).

class _Reduction {
  const _Reduction();
  _Bigint _convert(_Bigint x) => x;
  _Bigint _revert(_Bigint x) => x;

  void _mulTo(_Bigint x, _Bigint y, _Bigint r) {
    x._mulTo(y, r);
  }

  void _sqrTo(_Bigint x, _Bigint r) {
    x._sqrTo(r);
  }
}

// Montgomery reduction on _Bigint.
class _Montgomery implements _Reduction {
  final _Bigint _m;
  var _mp;
  var _mpl;
  var _mph;
  var _um;
  var _mused2;

  _Montgomery(this._m) {
    _mp = _m._invDigit();
    _mpl = _mp & _Bigint.DIGIT2_MASK;
    _mph = _mp >> _Bigint.DIGIT2_BITS;
    _um = (1 << (_Bigint.DIGIT_BITS - _Bigint.DIGIT2_BITS)) - 1;
    _mused2 = 2*_m._used;
  }

  // Return x*R mod _m
  _Bigint _convert(_Bigint x) {
    var r = new _Bigint();
    x.abs()._dlShiftTo(_m._used, r);
    r._divRemTo(_m, null, r);
    if (x._neg && !r._neg && r._used > 0) {
      _m._subTo(r, r);
    }
    return r;
  }

  // Return x/R mod _m
  _Bigint _revert(_Bigint x) {
    var r = new _Bigint();
    x._copyTo(r);
    _reduce(r);
    return r;
  }

  // x = x/R mod _m
  void _reduce(_Bigint x) {
    x._ensureLength(_mused2 + 1);
    while (x._used <= _mused2) {  // Pad x so _am has enough room later.
      x._digits[x._used++] = 0;
    }
    for (var i = 0; i < _m._used; ++i) {
      // Faster way of calculating u0 = x[i]*mp mod DIGIT_BASE.
      var j = x._digits[i] & _Bigint.DIGIT2_MASK;
      var u0 = (j*_mpl + (((j*_mph + (x._digits[i] >> _Bigint.DIGIT2_BITS)
          *_mpl) & _um) << _Bigint.DIGIT2_BITS)) & _Bigint.DIGIT_MASK;
      // Use _am to combine the multiply-shift-add into one call.
      j = i + _m._used;
      var digit = x._digits[j];
      digit += _m ._am(0, u0, x, i, _m._used);
      // Propagate carry.
      while (digit >= _Bigint.DIGIT_BASE) {
        digit -= _Bigint.DIGIT_BASE;
        x._digits[j++] = digit;
        digit = x._digits[j];
        digit++;
      }
      x._digits[j] = digit;
    }
    x._clamp();
    x._drShiftTo(_m ._used, x);
    if (x._compareTo(_m ) >= 0) {
      x._subTo(_m , x);
    }
  }

  // r = x^2/R mod _m ; x != r
  void _sqrTo(_Bigint x, _Bigint r) {
    x._sqrTo(r);
    _reduce(r);
  }

  // r = x*y/R mod _m ; x, y != r
  void _mulTo(_Bigint x, _Bigint y, _Bigint r) {
    x._mulTo(y, r);
    _reduce(r);
  }
}

