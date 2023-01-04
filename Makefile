MYDEFS = -g -Wall -std=c++11 -DLOCALHOST=\"127.0.0.1\"
RDIR = resources
RESOURCES = ${RDIR}/my_readwrite.h ${RDIR}/my_readwrite.cpp ${RDIR}/my_socket.h ${RDIR}/my_socket.cpp ${RDIR}/util.h ${RDIR}/util.cpp ${RDIR}/my_timestamp.h ${RDIR}/my_timestamp.cpp ${RDIR}/md5-calc.h ${RDIR}/md5-calc.cpp

RESOURCESCPP = ${RDIR}/my_readwrite.cpp ${RDIR}/my_socket.cpp ${RDIR}/util.cpp ${RDIR}/my_timestamp.cpp ${RDIR}/md5-calc.cpp

pa5: pa5.cpp ${RESOURCES}
	g++ ${MYDEFS} -o $@ ${RESOURCESCPP} $< -lcrypto -lpthread

clean:
	rm -f *.o pa5