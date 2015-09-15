/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/**
 * Copyright (c) 2013-2015 Regents of the University of California.
 *
 * This file is part of ndn-cxx library (NDN C++ library with eXperimental eXtensions).
 *
 * ndn-cxx library is free software: you can redistribute it and/or modify it under the
 * terms of the GNU Lesser General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later version.
 *
 * ndn-cxx library is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE.  See the GNU Lesser General Public License for more details.
 *
 * You should have received copies of the GNU General Public License and GNU Lesser
 * General Public License along with ndn-cxx, e.g., in COPYING.md file.  If not, see
 * <http://www.gnu.org/licenses/>.
 *
 * See AUTHORS.md for complete list of ndn-cxx authors and contributors.
 *
 * @author Alexander Afanasyev <http://lasr.cs.ucla.edu/afanasyev/index.html>
 */

// correct way to include ndn-cxx headers
// #include <ndn-cxx/face.hpp>
#include <ndn-cxx/face.hpp>
#include <json/reader.h>
#include <json/value.h>
#include <json/writer.h>
#include <iostream>

// Enclosing code in ndn simplifies coding (can also use `using namespace ndn`)
namespace ndn {
// Additional nested namespace could be used to prevent/limit name contentions
namespace examples {

class Consumer : noncopyable
{
public:
  void
  run()
  {
    // add incorrect filters
    Json::Value badJson;
    badJson["activity"] = Json::nullValue;
    std::cout << badJson.toStyledString() << std::endl;
    //Json::FastWriter fastWriter;
    //const std::string resStr = fastWriter.write(badJsson);
    //std::string resStr = Json::FastWriter().write(badJson);
    //std::cout << "resStr " << resStr << std::endl;
    std::string resStr("{\n\"activity\": \n}");
    ndn::name::Component tmp (reinterpret_cast<const uint8_t*>(resStr.data()), resStr.length());
    Interest interest(Name("/catalog/laptop/query").append(tmp));
    //Interest interest(Name("/catalog/myUniqueName/filters-initialization"));
    //interest.setInterestLifetime(time::milliseconds(1000));
    interest.setMustBeFresh(true);

    m_face.expressInterest(interest,
                           bind(&Consumer::onData, this,  _1, _2),
                           bind(&Consumer::onTimeout, this, _1));

    std::cout << "Sending " << interest << std::endl;

    // processEvents will block until the requested data received or timeout occurs
    m_face.processEvents();
  }

private:
  void
  onData(const Interest& interest, const Data& data)
  {
    std::cout << data << std::endl;
    std::string message(reinterpret_cast<const char*>(data.getContent().value()),
                        data.getContent().value_size());
    Json::Reader reader;
    Json::Value value;
    if (!reader.parse(message, value)) {
      std::cout << "malformed json" << std::endl;
    }

    std::cout << value.toStyledString() << std::endl;
  }

  void
  onTimeout(const Interest& interest)
  {
    std::cout << "Timeout " << interest << std::endl;
  }

private:
  Face m_face;
};

} // namespace examples
} // namespace ndn

int
main(int argc, char** argv)
{
  ndn::examples::Consumer consumer;
  try {
    consumer.run();
  }
  catch (const std::exception& e) {
    std::cerr << "ERROR: " << e.what() << std::endl;
  }
  return 0;
}