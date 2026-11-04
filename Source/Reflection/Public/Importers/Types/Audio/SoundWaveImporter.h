#pragma once

#include "Importers/Constructor/Importer.h"

class ISoundWaveImporter : public IImporter {
public:
	virtual bool Import() override;
};

REGISTER_IMPORTER(ISoundWaveImporter, {
	"SoundWave"
}, "Sound Assets");
