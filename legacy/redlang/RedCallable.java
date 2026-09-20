
package redlang;

import java.util.List;

interface RedCallable {

  int arity();

  Object call(Interpreter interpreter, List<Object> arguments);
}
