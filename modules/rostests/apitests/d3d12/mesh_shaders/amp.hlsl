struct Payload { float blue; };
groupshared Payload payload;
[numthreads(1, 1, 1)]
void main()
{
    payload.blue = 1.0;
    DispatchMesh(1, 1, 1, payload);
}
