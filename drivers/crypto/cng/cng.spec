@ stdcall BCryptCloseAlgorithmProvider(ptr long) ksecdd.BCryptCloseAlgorithmProvider
@ stdcall BCryptCreateHash(ptr ptr ptr long ptr long long) ksecdd.BCryptCreateHash
@ stdcall BCryptDecrypt(ptr ptr long ptr ptr long ptr long ptr long) ksecdd.BCryptDecrypt
@ stdcall BCryptDestroyHash(ptr) ksecdd.BCryptDestroyHash
@ stdcall BCryptDestroyKey(ptr) ksecdd.BCryptDestroyKey
@ stdcall BCryptEncrypt(ptr ptr long ptr ptr long ptr long ptr long) ksecdd.BCryptEncrypt
@ stdcall BCryptFinishHash(ptr ptr long long) ksecdd.BCryptFinishHash
@ stdcall BCryptGenRandom(ptr ptr long long) ksecdd.BCryptGenRandom
@ stdcall BCryptGenerateSymmetricKey(ptr ptr ptr long ptr long long) ksecdd.BCryptGenerateSymmetricKey
@ stdcall BCryptGetProperty(ptr wstr ptr long ptr long) ksecdd.BCryptGetProperty
@ stdcall BCryptHashData(ptr ptr long long) ksecdd.BCryptHashData
@ stdcall BCryptImportKey(ptr ptr wstr ptr ptr long ptr long long) ksecdd.BCryptImportKey
@ stdcall BCryptImportKeyPair(ptr ptr wstr ptr ptr long long) ksecdd.BCryptImportKeyPair
@ stdcall BCryptKeyDerivation(ptr ptr ptr long ptr long) ksecdd.BCryptKeyDerivation
@ stdcall BCryptOpenAlgorithmProvider(ptr wstr wstr long) ksecdd.BCryptOpenAlgorithmProvider
@ stdcall BCryptSetProperty(ptr wstr ptr long long) ksecdd.BCryptSetProperty
@ stdcall BCryptVerifySignature(ptr ptr ptr long ptr long long) ksecdd.BCryptVerifySignature
@ stdcall BCryptGenerateKeyPair(ptr ptr long long) ksecdd.BCryptGenerateKeyPair
@ stdcall BCryptFinalizeKeyPair(ptr long) ksecdd.BCryptFinalizeKeyPair
@ stdcall BCryptExportKey(ptr ptr wstr ptr long ptr long) ksecdd.BCryptExportKey
@ stdcall BCryptSecretAgreement(ptr ptr ptr long) ksecdd.BCryptSecretAgreement
@ stdcall BCryptDeriveKey(ptr wstr ptr ptr long ptr long) ksecdd.BCryptDeriveKey
@ stdcall BCryptDestroySecret(ptr) ksecdd.BCryptDestroySecret
@ cdecl SystemPrng(ptr long) ksecdd.SystemPrng
