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
#pragma once
#include <string>
#include <vector>
#include <unordered_map>

namespace wsp {

using URIPath = std::vector< std::string >;
using Request = std::unordered_map< std::string, std::string >;
using URIParameters = std::unordered_multimap< std::string, std::string >;

URIPath UriPath(const std::string& path); 
bool Has(const Request& req, const std::string& key);
const std::string& Get(const Request& req, const std::string& key);
URIParameters UriParameters(const std::string& params);
std::string FileExtension(const std::string& filepath);
const std::string& GetMimeType(const std::string& ext);
int GetContentSize(const Request&);

}
