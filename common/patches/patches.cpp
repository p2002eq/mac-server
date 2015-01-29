
#include "../global_define.h"
#include "patches.h"

#include "mac.h"
#include "evolution.h"

void RegisterAllPatches(EQStreamIdentifier &into) {
	Mac::Register(into);
	Evolution::Register(into);

}

void ReloadAllPatches() {
	Mac::Reload();
	Evolution::Reload();
}

