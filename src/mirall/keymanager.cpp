#include "keymanager.h"

Keymanager* Keymanager::_Instance = NULL;


Keymanager* Keymanager::Instance()
{
    if (!_Instance) {
        _Instance = new Keymanager();
    }

    return _Instance;
}

RSA* Keymanager::getRSAkey()
{
    return this->rsa;
}

void Keymanager::setRSAkey(RSA *rsa)
{
    this->rsa = rsa;
}
