//
// Created by Ugo Varetto on 7/20/16.
//


//find position of specific type in variadic argument list

#include <cstdlib>
#include <iostream>
#include <cassert>
#include <functional>

using namespace std;


int foo(int i = 4) { return i; }

int main(int, char**) {


    function< int (int) > f = [](int i = 7) { return i; };
    cout << f(3) << endl  << endl;
    function< int (int) > F = foo;
    cout << F();
    return EXIT_SUCCESS;
}