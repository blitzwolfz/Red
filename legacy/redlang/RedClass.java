
package redlang;

import java.util.List;
import java.util.Map;



class RedClass implements RedCallable {

  final String name;

  final RedClass superclass;



  private final Map<String, RedFunction> methods;



  RedClass(String name, RedClass superclass,
           Map<String, RedFunction> methods) {
    this.superclass = superclass;

    this.name = name;
    this.methods = methods;
  }


  RedFunction findMethod(String name) {
    if (methods.containsKey(name)) {
      return methods.get(name);
    }


    if (superclass != null) {
      return superclass.findMethod(name);
    }


    return null;
  }


  @Override
  public String toString() {
    return name;
  }

  @Override
  public Object call(Interpreter interpreter,
                     List<Object> arguments) {
    RedInstance instance = new RedInstance(this);

    RedFunction initializer = findMethod("init");
    if (initializer != null) {
      initializer.bind(instance).call(interpreter, arguments);
    }


    return instance;
  }

  @Override
  public int arity() {


    RedFunction initializer = findMethod("init");
    if (initializer == null) return 0;
    return initializer.arity();

  }

}
