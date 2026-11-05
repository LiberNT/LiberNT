/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tells whether a device runs on the virtio 3D GPU, whose host has measured limits
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */
#pragma once

static inline BOOL DeviceIsVirtio3d(ID3D11Device *Device)
{
    IDXGIDevice *Dxgi = NULL;
    IDXGIAdapter *Adapter = NULL;
    DXGI_ADAPTER_DESC Desc = {};

    if (SUCCEEDED(Device->QueryInterface(IID_IDXGIDevice, reinterpret_cast<void **>(&Dxgi))))
    {
        if (SUCCEEDED(Dxgi->GetAdapter(&Adapter)))
        {
            Adapter->GetDesc(&Desc);
            Adapter->Release();
        }
        Dxgi->Release();
    }
    return Desc.VendorId == 0x1af4 && Desc.DeviceId == 0x1050;
}
