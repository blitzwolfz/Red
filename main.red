var a = 1;

fun printSum(yo, b) {
    return yo+b;
}

fun whoAmI(yo) {
    print yo;
}

class Animal {

    init(animal) {
        this.animal = animal;
    }

    me() {
        print "I am a " + this.animal;
    }
    roar() {
        print "ROOOOOOAR";
    }

    eat(who) {
        print "Bruh I ain't eating " + who;
    }
}

class DrunkAnimal < Animal {
    drink () {
        print "Yes I drink a lot";
    }
}

for (var x = 0; x < 5; x = x + 1) {
    whoAmI(printSum(a, x));
}

var ani = Animal("Tiger");
ani.me();
ani.roar();
ani.eat("Nut");

var ani2 = DrunkAnimal("Tigger");
ani2.me();
ani2.roar();
ani2.eat("Nut");
ani2.drink();

print clock();