/*
    This file is part of Helio Workstation.

    Helio is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    Helio is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Helio. If not, see <http://www.gnu.org/licenses/>.
*/

#include "Common.h"
#include "AudioCore.h"
#include "InternalPluginFormat.h"
#include "BuiltInSynthFormat.h"
#include "PluginWindow.h"
#include "OrchestraPit.h"
#include "Instrument.h"
#include "SerializationKeys.h"
#include "AudioMonitor.h"
#include "AudiobusOutput.h"

void AudioCore::initAudioFormats(AudioPluginFormatManager &formatManager)
{
    formatManager.addDefaultFormats();
    formatManager.addFormat(new InternalPluginFormat());
    formatManager.addFormat(new BuiltInSynthFormat());
}

AudioCore::AudioCore()
{
    Logger::writeToLog("AudioCore::AudioCore");

    this->audioMonitor = new AudioMonitor();
    this->deviceManager.addAudioCallback(this->audioMonitor);

    AudioCore::initAudioFormats(this->formatManager);

#if HELIO_AUDIOBUS_SUPPORT
    AudiobusOutput::init();
#endif
}

AudioCore::~AudioCore()
{
#if HELIO_AUDIOBUS_SUPPORT
    AudiobusOutput::shutdown();
#endif

    this->deviceManager.removeAudioCallback(this->audioMonitor);
    this->audioMonitor = nullptr;

    //ScopedPointer<XmlElement> test(this->metaInstrument->serialize());
    //DocumentReader::saveObfuscated(File("111.txt"), test);

    this->deviceManager.closeAudioDevice();
    this->masterReference.clear();
}

void AudioCore::mute()
{
    for (auto instrument : this->instruments)
    {
        this->removeInstrumentFromDevice(instrument);
    }
}

void AudioCore::unmute()
{
    this->mute(); // на всякий случай, чтоб 2 раза инструменты не добавлялись

    for (auto instrument : this->instruments)
    {
        this->addInstrumentToDevice(instrument);
    }
}

AudioDeviceManager &AudioCore::getDevice() noexcept
{
    return this->deviceManager;
}

AudioPluginFormatManager &AudioCore::getFormatManager() noexcept
{
    return this->formatManager;
}

AudioMonitor *AudioCore::getMonitor() const noexcept
{
    return this->audioMonitor;
}

//===----------------------------------------------------------------------===//
// Instruments
//===----------------------------------------------------------------------===//

Instrument *AudioCore::addInstrument(const PluginDescription &pluginDescription,
                                     const String &name)
{
    auto instrument = new Instrument(formatManager, name);
    this->addInstrumentToDevice(instrument);

    instrument->initializeFrom(pluginDescription);
    this->instruments.add(instrument);

    this->broadcastInstrumentAdded(instrument);

    return instrument;
}

void AudioCore::removeInstrument(Instrument *instrument)
{
    this->broadcastInstrumentRemoved(instrument);

    this->removeInstrumentFromDevice(instrument);
    this->instruments.removeObject(instrument, true);

    this->broadcastInstrumentRemovedPostAction();
}

void AudioCore::addInstrumentToDevice(Instrument *instrument)
{
    this->deviceManager.addAudioCallback(&instrument->getProcessorPlayer());
    this->deviceManager.addMidiInputCallback(String::empty, &instrument->getProcessorPlayer().getMidiMessageCollector());
}

void AudioCore::removeInstrumentFromDevice(Instrument *instrument)
{
    this->deviceManager.removeAudioCallback(&instrument->getProcessorPlayer());
    this->deviceManager.removeMidiInputCallback(String::empty, &instrument->getProcessorPlayer().getMidiMessageCollector());
}

//===----------------------------------------------------------------------===//
// OrchestraPit
//===----------------------------------------------------------------------===//

Array<Instrument *> AudioCore::getInstruments() const
{
    Array<Instrument *> result;
    result.addArray(this->instruments);
    return result;
}

Instrument *AudioCore::findInstrumentById(const String &id) const
{
    // check by ids
    for (int i = 0; i < this->instruments.size(); ++i)
    {
        Instrument *instrument = this->instruments.getUnchecked(i);

        if (id.contains(instrument->getInstrumentID()))
        {
            return instrument;
        }
    }

    // check by hashes
    for (int i = 0; i < this->instruments.size(); ++i)
    {
        Instrument *instrument = this->instruments.getUnchecked(i);

        if (id.contains(instrument->getInstrumentHash()))
        {
            return instrument;
        }
    }

    return nullptr;
}

void AudioCore::initDefaultInstrument()
{
    OwnedArray<PluginDescription> descriptions;

    BuiltInSynthFormat format;
    format.findAllTypesForFile(descriptions, BuiltInSynth::pianoId);

    PluginDescription desc(*descriptions[0]);
    this->addInstrument(desc, "Default");
}

//===----------------------------------------------------------------------===//
// Setup
//===----------------------------------------------------------------------===//

void AudioCore::autodetectDeviceSetup()
{
    Logger::writeToLog("AudioCore::autodetectDeviceSetup");
    
    // requesting 0 inputs and only 2 outputs because of freaking alsa
    this->deviceManager.initialise(0, 2, nullptr, true);

    const auto deviceType =
        this->deviceManager.getCurrentDeviceTypeObject();

    const auto device =
        this->deviceManager.getCurrentAudioDevice();

    if (!deviceType || !device)
    {
        const OwnedArray<AudioIODeviceType> &types =
            this->deviceManager.getAvailableDeviceTypes();

        AudioIODeviceType *const type = types[0];

        this->deviceManager.setCurrentAudioDeviceType(type->getTypeName(), true);

        type->scanForDevices();

        AudioDeviceManager::AudioDeviceSetup deviceSetup;
        this->deviceManager.getAudioDeviceSetup(deviceSetup);
        this->deviceManager.setAudioDeviceSetup(deviceSetup, true);
    }
}

ValueTree AudioCore::serializeDeviceManager() const
{
    using namespace Serialization;

    ValueTree tree(Audio::audioDevice);
    AudioDeviceManager::AudioDeviceSetup currentSetup;
    this->deviceManager.getAudioDeviceSetup(currentSetup);

    tree.setProperty(Audio::audioDeviceType, this->deviceManager.getCurrentAudioDeviceType());
    tree.setProperty(Audio::audioOutputDeviceName, currentSetup.outputDeviceName);
    tree.setProperty(Audio::audioInputDeviceName, currentSetup.inputDeviceName);

    const auto currentAudioDevice = this->deviceManager.getCurrentAudioDevice();
    if (currentAudioDevice != nullptr)
    {
        tree.setProperty(Audio::audioDeviceRate,
            currentAudioDevice->getCurrentSampleRate());

        if (currentAudioDevice->getDefaultBufferSize() !=
            currentAudioDevice->getCurrentBufferSizeSamples())
        {
            tree.setProperty(Audio::audioDeviceBufferSize,
                currentAudioDevice->getCurrentBufferSizeSamples());
        }

        if (!currentSetup.useDefaultInputChannels)
        {
            tree.setProperty(Audio::audioDeviceInputChannels,
                currentSetup.inputChannels.toString(2));
        }

        if (!currentSetup.useDefaultOutputChannels)
        {
            tree.setProperty(Audio::audioDeviceOutputChannels,
                currentSetup.outputChannels.toString(2));
        }
    }

    const StringArray availableMidiDevices(MidiInput::getDevices());
    for (const auto &midiInputName : availableMidiDevices)
    {
        if (this->deviceManager.isMidiInputEnabled(midiInputName))
        {
            ValueTree midiInputNode(Audio::midiInput);
            midiInputNode.setProperty(Audio::midiInputName, midiInputName);
            tree.appendChild(midiInputNode);
        }
    }

    // Add any midi devices that have been enabled before, but which aren't currently
    // open because the device has been disconnected:
    if (!this->customMidiInputs.isEmpty())
    {
        for (const auto &midiInputName : this->customMidiInputs)
        {
            if (!availableMidiDevices.contains(midiInputName, true))
            {
                ValueTree midiInputNode(Audio::midiInput);
                midiInputNode.setProperty(Audio::midiInputName, midiInputName);
                tree.appendChild(midiInputNode);
            }
        }
    }

    const String defaultMidiOutput(this->deviceManager.getDefaultMidiOutputName());
    if (defaultMidiOutput.isNotEmpty())
    {
        tree.setProperty(Audio::defaultMidiOutput, defaultMidiOutput);
    }

    return tree;
}

void AudioCore::deserializeDeviceManager(const ValueTree &tree)
{
    using namespace Serialization;

    const auto root = tree.hasType(Audio::audioDevice) ?
        tree : tree.getChildWithName(Audio::audioDevice);

    if (!root.isValid())
    {
        this->autodetectDeviceSetup();
        return;
    }

    // A hack: this will call scanDevicesIfNeeded():
    const auto &availableDeviceTypes = this->deviceManager.getAvailableDeviceTypes();

    String error;
    AudioDeviceManager::AudioDeviceSetup setup;
    setup.inputDeviceName = root.getProperty(Audio::audioInputDeviceName);
    setup.outputDeviceName = root.getProperty(Audio::audioOutputDeviceName);

    String currentDeviceType = root.getProperty(Audio::audioDeviceType);
    AudioIODeviceType *foundType = nullptr;

    for (const auto availableType : availableDeviceTypes)
    {
        if (availableType->getTypeName() == currentDeviceType)
        {
            foundType = availableType;
        }
    }

    if (foundType == nullptr && !availableDeviceTypes.isEmpty())
    {
        // TODO search for device types with the same i/o device names?
        currentDeviceType = availableDeviceTypes.getFirst()->getTypeName();
    }

    this->deviceManager.setCurrentAudioDeviceType(currentDeviceType, true);

    setup.bufferSize = root.getProperty(Audio::audioDeviceBufferSize, setup.bufferSize);
    setup.sampleRate = root.getProperty(Audio::audioDeviceRate, setup.sampleRate);

    const var defaultTwoChannels("11");
    const String inputChannels = root.getProperty(Audio::audioDeviceInputChannels, defaultTwoChannels);
    const String outputChannels = root.getProperty(Audio::audioDeviceOutputChannels, defaultTwoChannels);
    setup.inputChannels.parseString(inputChannels, 2);
    setup.outputChannels.parseString(outputChannels, 2);

    setup.useDefaultInputChannels = !root.hasProperty(Audio::audioDeviceInputChannels);
    setup.useDefaultOutputChannels = !root.hasProperty(Audio::audioDeviceOutputChannels);

    error = this->deviceManager.setAudioDeviceSetup(setup, true);

    this->customMidiInputs.clearQuick();
    forEachValueTreeChildWithType(root, c, Audio::midiInput)
    {
        this->customMidiInputs.add(c.getProperty(Audio::midiInputName));
    }

    const StringArray allMidiIns(MidiInput::getDevices());
    for (const auto &midiIn : allMidiIns)
    {
        this->deviceManager.setMidiInputEnabled(midiIn,
            this->customMidiInputs.contains(midiIn));
    }

    if (error.isNotEmpty())
    {
        error = this->deviceManager.initialise(0, 2, nullptr, false);
    }

    this->deviceManager.setDefaultMidiOutput(root.getProperty(Audio::defaultMidiOutput));
}

//===----------------------------------------------------------------------===//
// Serializable
//===----------------------------------------------------------------------===//

ValueTree AudioCore::serialize() const
{
    Logger::writeToLog("AudioCore::serialize");
    using namespace Serialization;

    // serializes all settings and instruments (with their graphs)
    // deviceManager's graph is not serialized but managed dynamically

    ValueTree tree(Audio::audioCore);
    ValueTree orchestra(Audio::orchestra);
    for (int i = 0; i < this->instruments.size(); ++i)
    {
        Instrument *instrument = this->instruments.getUnchecked(i);
        orchestra.appendChild(instrument->serialize());
    }

    tree.appendChild(orchestra);

    const auto deviceState(this->serializeDeviceManager());
    tree.appendChild(deviceState);
    return tree;
}

void AudioCore::deserialize(const ValueTree &tree)
{
    Logger::writeToLog("AudioCore::deserialize");
    using namespace Serialization;

    // re-creates deviceManager's graph each time on de-serialization
    this->reset();

    const auto root = tree.hasType(Audio::audioCore) ?
        tree : tree.getChildWithName(Audio::audioCore);

    if (!root.isValid())
    {
        this->autodetectDeviceSetup();
        return;
    }

    this->deserializeDeviceManager(root);

    const auto orchestra = root.getChildWithName(Audio::orchestra);
    if (orchestra.isValid())
    {
        for (const auto &instrumentNode : orchestra)
        {
            Instrument *instrument = new Instrument(this->formatManager, "");
            this->addInstrumentToDevice(instrument);
            instrument->deserialize(instrumentNode);
            this->instruments.add(instrument);
        }
    }
}

void AudioCore::reset()
{
    while (this->instruments.size() > 0)
    {
        this->removeInstrument(this->instruments[0]);
    }
}
