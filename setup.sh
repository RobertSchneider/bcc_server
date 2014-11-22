mkdir temp_bcc
cd temp_bcc
git clone https://github.com/json-c/json-c
cd json-c
sh autogen.sh
./configure
make
make install
cd ..

git clone -b development https://github.com/OSpringer/bcc-library/
cd bcc-library
mv /usr/local/include/json-c /usr/local/include/json
make
sudo cp libbcc.so /usr/local/lib/
mkdir /usr/local/include/bcc
sudo cp include/bcc.h /usr/local/include/bcc/
