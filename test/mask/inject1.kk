// Test mask

fun foo(action : () -> e a ) : <ndet|e> a
{
  val x = mask<ndet>(action)
  if (False) then foo(action) else x 
}

fun main() {
  foo{42}.println
}