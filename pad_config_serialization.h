#pragma once
class PadTranslator;
class Serializer;
class Deserializer;
// arcade_play legacy Flash format; not part of ea_input.
void serializePadTranslator(const PadTranslator &, Serializer &);
void deserializePadTranslator(PadTranslator &, Deserializer &);
