/*

*/

public import std/num/double


fun check(name : string, res : string, got : string ) : io () {
  println(name.pad-right(7,' ') ++ ": " 
    ++ (if (got == res) then "ok: " ++ res 
                       else "FAILED!:\n expect: " ++ res ++ "\n gotten: " ++ got ++ "\n"))
  // if (got != res) then error("failed") else ()
}

fun check2( name : string, res : string, tst : (double, int) ) : io () {
  check(name,res, tst.fst.show ++ ", " ++ tst.snd.show)
}

fun check1(name : string, res : string, tst : double ) : io () {
  check(name,res,tst.show)
}

fun check-logp(name : string, res : string, x : double ) : io () {
  check(name,res, log(x + 1.0).show ++ ", " ++ log1p(x).show ++ ", " ++ system-log1p(x).show)
}

extern system-log1p( x : double ) : double {
  c "log1p"
  js "Math.log1p"
}

// The area hyperbolic tangent of `d`
inline extern system-atanh( d : double ) : double {
  c "atanh"
  js "Math.atanh"
}

// The area hyperbolic cosine of `d`
inline extern system-acosh( d : double ) : double {
  c "acosh"
  js "Math.acosh"
}

// The area hyperbolic sine of `d`
inline extern system-asinh( d : double ) : double {
  c "asinh"
  js "Math.asinh"
}

fun check-eq( name : string, nums : list<double>, f : double -> double, g : double -> double) {
  val xs = nums ++ nums.map((~))
  xs.foreach fn(x:double) {
    val fx = f(x)
    val gx = g(x)
    check(name ++ ": " ++ x.show, fx.show(14), gx.show(14))
  }
}

fun main() {
  check-logp("logp 1", "0.69314718055994529, 0.69314718055994529, 0.69314718055994529", 1.0)
  check-logp("logp 2", "1.3322676295501871e-015, 1.2344999999999993e-015, 1.2344999999999993e-015", 1.2345e-15)
  check-logp("logp 3", "2.2204460492503128e-016, 1.2344999999999999e-016, 1.2344999999999999e-016", 1.2345e-16)
  check-logp("logp 4", "0, 1.2345e-032, 1.2345e-032", 1.2345e-32)
  check-logp("logp 5", "0, 1.2344999999999999e-064, 1.2344999999999999e-064", 1.2345e-64)
  check-logp("logp 6", "709.78271289338397, 709.78271289338397, 709.78271289338397", 1.7976931348622732e+308)
  check-eq("atanh", [0.0,1.23e-11,1.23e-10,1.23e-3,0.123,0.5,dbl-log2,1.0,2.0,posinf,nan], atanh, system-atanh)
  check-eq("asinh", [0.0,1.23e-16,1.23e-15,1.23e-3,0.122,0.5,dbl-log2,1.0,2.0,1.23e3,1.23e8,1.23e15,1.23e16,posinf,nan], asinh, system-asinh)
  check-eq("acosh", [0.0,1.23e-16,1.23e-15,1.23e-3,0.122,0.5,dbl-log2,1.0,2.0,1.23e3,1.23e8,1.23e15,1.23e16,posinf,nan], acosh, system-acosh)

}