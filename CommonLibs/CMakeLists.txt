#
# OpenBTS-UMTS/CommonLibs/CMakeLists.txt
#

add_library(openbts-umts-common
	BitVector.cpp
	TurboCoder.cpp
	ByteVector.cpp
	LinkedLists.cpp
	Sockets.cpp
	Threads.cpp
	Timeval.cpp
	Logger.cpp
	URLEncode.cpp
	Configuration.cpp
	sqlite3util.cpp
	Utils.cpp
)

target_link_libraries(openbts-umts-common sqlite3)

add_executable(BitVectorTest BitVectorTest.cpp)
target_link_libraries(BitVectorTest openbts-umts-common -pthread)

add_executable(ConfigurationTest ConfigurationTest.cpp)
target_link_libraries(ConfigurationTest openbts-umts-common -pthread)

add_executable(F16Test F16Test.cpp)

add_executable(InterthreadTest InterthreadTest.cpp)
target_link_libraries(InterthreadTest openbts-umts-common -pthread)

add_executable(LogTest LogTest.cpp)
target_link_libraries(LogTest openbts-umts-common -pthread)

add_executable(RegexpTest RegexpTest.cpp)
target_link_libraries(RegexpTest openbts-umts-common)

add_executable(SocketsTest SocketsTest.cpp)
target_link_libraries(SocketsTest openbts-umts-common -pthread)

add_executable(TimevalTest TimevalTest.cpp)
target_link_libraries(TimevalTest openbts-umts-common)

add_executable(URLEncodeTest URLEncodeTest.cpp)
target_link_libraries(URLEncodeTest openbts-umts-common)

add_executable(VectorTest VectorTest.cpp)
target_link_libraries(VectorTest openbts-umts-common -pthread)

#set(EXTRA_DIST
#	example.config
#	README.common
#)
