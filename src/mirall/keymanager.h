#ifndef KEYMANAGER_H
#define KEYMANAGER_H

#include <openssl/rsa.h>

class Keymanager
{
public:
    static Keymanager* Instance();
    RSA* getRSAkey();
    void setRSAkey(RSA *rsa);

private:
    RSA *rsa;

    Keymanager() {};
    Keymanager(Keymanager const&){};
    Keymanager& operator=(Keymanager const&){};
    static Keymanager* _Instance;

};

#endif // KEYMANAGER_H
