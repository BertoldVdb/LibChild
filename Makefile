#Copyright (c) 2018, Bertold Van den Bergh
#All rights reserved.
#
#Redistribution and use in source and binary forms, with or without
#modification, are permitted provided that the following conditions are met:
#    * Redistributions of source code must retain the above copyright
#      notice, this list of conditions and the following disclaimer.
#    * Redistributions in binary form must reproduce the above copyright
#      notice, this list of conditions and the following disclaimer in the
#      documentation and/or other materials provided with the distribution.
#    * Neither the name of the author nor the
#      names of its contributors may be used to endorse or promote products
#      derived from this software without specific prior written permission.
#
#THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
#ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
#WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
#DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR DISTRIBUTOR BE LIABLE FOR ANY
#DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
#(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
#LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
#ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
#(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
#SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.


CCARCH=
CC=$(CCARCH)gcc
AR=$(CCARCH)ar
STRIP=$(CCARCH)strip

CFLAGS=-fPIC -O3 -Wall -c -fmessage-length=0 -Werror -ffunction-sections -fdata-sections -fvisibility=hidden
LDFLAGS=-shared -fvisibility=hidden

EXECUTABLE=libchild.so
EXECUTABLE_STATIC=libchild.a
INCLUDES_SRC=def.h libchild.h
SOURCES_SRC=libchild.c slave.c socket.c priv.c


OBJECTS_OBJ=$(SOURCES_SRC:.c=.o)

all: $(EXECUTABLE) $(EXECUTABLE_STATIC)

$(EXECUTABLE): $(OBJECTS_OBJ)
	$(CC) $(LDFLAGS) $(OBJECTS_OBJ) -o $@
	$(STRIP) -x $@

$(EXECUTABLE_STATIC): $(OBJECTS_OBJ)
	$(AR) rcs $(EXECUTABLE_STATIC) $(OBJECTS_OBJ)
	

obj/%.o: src/%.c $(INCLUDES_SRC)
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -rf $(OBJECTS_OBJ) $(EXECUTABLE) $(EXECUTABLE_STATIC)
