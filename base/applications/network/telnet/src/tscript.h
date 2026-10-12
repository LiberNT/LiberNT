#pragma once

//#include <windows.h>
//#include <stdio.h>

#include "tnetwork.h"

class TScript {
public:
	TScript(TNetwork &RefNetwork) {fp = NULL;}
	~TScript() {}
	BOOL processScript(char *data);
	void initScript(char *filename);
private:
	FILE *fp;
};
