// Websockets+ : C++11 server-side websocket library based on libwebsockets;
//               supports easy creation of services and built-in throttling
// Copyright (C) 2014  Ugo Varetto
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
#include <fstream>
#include <string>
#include <sstream>
#include <iostream>
using namespace std;
int main(int argc, char** argv) {
    if(argc < 4) {
        std::cout << "usage: " << argv[0] 
                  << "<in file> <ext -> mime file> <mime -> ext file>\n";
        return 0;          
    }
    ifstream is(argv[1]);
    ofstream osf(argv[2]);
    ofstream osb(argv[3]);
    string line;
    while(getline(is, line)) {
        if(line.size() < 1) continue;
        istringstream iss(line);
        string key, value;
        iss >> key >> value;
        osf << "{\"" << key << "\", \"" << value << "\"},\n";
        osb << "{\"" << value << "\", \"" << key << "\"},\n";
    }
    return 0;
}