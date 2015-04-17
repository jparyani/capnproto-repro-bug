#!/bin/bash

bin/server 127.0.0.1:33131&
bin/client 127.0.0.1:33131
kill $!
