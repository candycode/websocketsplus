//
// Created by Ugo Varetto on 7/20/16.
//


//find position of specific type in variadic argument list

#include <cstdlib>
#include <tuple>
#include <cassert>

using namespace std;


//Helper class

//template:
//I current index
//T type to search for
//H head of type list
//TailT tail of type list
template < int I, typename T, typename H, typename...TailT >
struct MapHelper {
    enum {id = MapHelper< I + 1, T, TailT... >::id};
};

//template specialization: type found case
//I current index assigned to id field
//T type to search for
//ArgsT type list: head type matches T
template < int I, typename T, typename... ArgsT >
struct MapHelper< I, T, T, ArgsT... > {
    enum {id = I};
};

//template specialization: termination condition: last type matches T
template < int I, typename T >
struct MapHelper< I, T, T > {
    enum {id = I};
};

//template specialization: termination condition: last type does not match T
//set id to -1 (not found)
template < int I, typename T, typename U >
struct MapHelper< I, T, U> {
    enum {id = -1};
};

//Map type to position in type list
template < typename T, typename... ArgsT >
struct Map {
    enum {id = MapHelper< 0,  T, ArgsT... >::id};
};


int main(int, char**) {

    static_assert(Map< int, float, int >::id == 1, "Wrong index");
    static_assert(Map< int, int, char >::id == 0, "Wrong index");
    static_assert(Map< char, int, char >::id == 1, "Wrong index");
    static_assert(Map< char, float, int, double >::id == -1, "Wrong index");

    return EXIT_SUCCESS;
}