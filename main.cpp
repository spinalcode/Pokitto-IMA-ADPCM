
#define my_TIMER_16_0_IRQn 16          // for Timer setup
#include "mbed.h"
#include "Pokitto.h"
#include <LibAudio>
#include <File>
#include "timer_11u6x.h"
#include "clock_11u6x.h"

using PC = Pokitto::Core;
using PD = Pokitto::Display;
using PB = Pokitto::Buttons;

uint8_t ADPCMBuffer[4096]; // 44100 sample rate uses 1024 byte packets I think.
int outputCount=0;
File wavFile;
//File pcmFile;
bool FileIsOpen = false;

#include "adpcm.h"

//long int fileDataOffset = 0;
int LISTENINGSAMPLERATE = 8000;
uint8_t myByte=0;


void setVol(uint32_t v){
    v = 255 - (192 - v) * (192 - v) * 255 / 36864;
    uint32_t hwVolume = v ? (v>>1) | 0xF : 0;
    uint32_t swVolume = v ? (v | 0xF) + 1 : 0;
    SoftwareI2C(P0_4, P0_5).write(0x5e, hwVolume);
    SoftwareI2C(P0_5, P0_4).write(0x5e, hwVolume); // fix for newer boards with i2C right way around
}

// writeDAC() from Pokitto MiniLib
inline void writeDAC(unsigned char out) {
    volatile unsigned char* P1 = (unsigned char*)(0xa0000020);
    volatile unsigned char* P2 = (unsigned char*)(0xa0000040);
    P1[28] = out & 1; out >>= 1;
    P1[29] = out & 1; out >>= 1;
    P1[30] = out & 1; out >>= 1;
    P1[31] = out & 1; out >>= 1;
    P2[20] = out & 1; out >>= 1;
    P2[21] = out & 1; out >>= 1;
    P2[22] = out & 1; out >>= 1;
    P2[23] = out;
}

int byteCount=0;
int nibbleCount=0;

static const int8_t indexTable[8] =
{ -1, -1, -1, -1, 2, 4, 6, 8 };

static const int16_t stepTable[89] =
{
	7, 8, 9, 10, 11, 12, 13, 14, 16, 17,
	19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
	50, 55, 60, 66, 73, 80, 88, 97, 107, 118,
	130, 143, 157, 173, 190, 209, 230, 253, 279, 307,
	337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
	876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066,
	2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358,
	5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899,
	15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767
};


//int audioBufferBlock = 0;
//int oldAudioBufferBlock = 0;
//int audioBufferOffset = 0;
int currentBuffer = 0;
int completeBuffer = 0;
int audioCount = 0;
int bufferOffset = 0;

void audioTimer(void){
	if (Chip_TIMER_MatchPending(LPC_TIMER16_0, 1)) {

        signed short sixteenBit;
    	if(byteCount == wavHeader.block_align){
    
    		byteCount = 4;
    		bufferOffset = wavHeader.block_align*(1-completeBuffer);
        	//wavFile.read(&prevsample, 2);
        	//wavFile.read(&previndex, 2);
    
            prevsample = *reinterpret_cast<const uint16_t*>(&ADPCMBuffer[bufferOffset]);
            prevsample = (signed short) prevsample;
            previndex = ADPCMBuffer[bufferOffset + 2];
            //audioBufferOffset=3;
    
    	}else{
    
        	if(nibbleCount == 0){
        	    nibbleCount = 1;
        		//wavFile.read(&myByte, 1);
                myByte = ADPCMBuffer[bufferOffset + byteCount];
        		sixteenBit = ImaAdpcmDecode(myByte & 15);
        	}else{
        		byteCount = byteCount + 1;	
        	    nibbleCount = 0;
        		sixteenBit = ImaAdpcmDecode((myByte >> 4) & 15);
                if(++audioCount >= wavHeader.block_align * 2){
                    audioCount=0;
                }
                currentBuffer = audioCount / wavHeader.block_align;
        	}
    	}    

        writeDAC(((sixteenBit+32768)>>8)&255);
/*
        int randomNumber = random(511);
        if ((sixteenBit & 0x01FF) > randomNumber)
            // put a 1 in the LSBit and truncate
            writeDAC( ((sixteenBit ^ 0x8000) | 0x0100) >> 8 );
        else
            // put a 0 in the LSBit and truncate
            writeDAC( ((sixteenBit ^ 0x8000) & 0xFE00) >> 8 );
*/

    	Chip_TIMER_ClearMatch(LPC_TIMER16_0, 1);
    }
}

// timer init stolen directly from Pokittolib
void initTimer(uint32_t sampleRate){
    /* Initialize 32-bit timer 0 clock */
	Chip_TIMER_Init(LPC_TIMER16_0);
    /* Timer rate is system clock rate */
	uint32_t timerFreq = Chip_Clock_GetSystemClockRate();
	/* Timer setup for match and interrupt at TICKRATE_HZ */
	Chip_TIMER_Reset(LPC_TIMER16_0);
	/* Enable both timers to generate interrupts when time matches */
	Chip_TIMER_MatchEnableInt(LPC_TIMER16_0, 1);
    /* Setup 32-bit timer's duration (32-bit match time) */
	Chip_TIMER_SetMatch(LPC_TIMER16_0, 1, (timerFreq / sampleRate));
	/* Setup both timers to restart when match occurs */
	Chip_TIMER_ResetOnMatchEnable(LPC_TIMER16_0, 1);
	/* Start both timers */
	Chip_TIMER_Enable(LPC_TIMER16_0);
	/* Clear both timers of any pending interrupts */
	NVIC_ClearPendingIRQ((IRQn_Type)my_TIMER_16_0_IRQn);
    /* Redirect IRQ vector - Jonne*/
    NVIC_SetVector((IRQn_Type)my_TIMER_16_0_IRQn, (uint32_t)&audioTimer);
	/* Enable both timer interrupts */
	NVIC_EnableIRQ((IRQn_Type)my_TIMER_16_0_IRQn);
}



void init() {
    setVol(3);
    PD::persistence = true;
    PD::invisiblecolor = 0;
    PD::fontSize=1;
    PD::fixedWidthFont = true;
    PD::adjustCharStep = 0;
    PD::adjustLineStep = 0;


    // lets open the file here
    if(wavFile.openRO("/rockandroll.wav")){
        FileIsOpen = true;

        wavFile.read(&wavHeader.riff, 4);
        wavFile.read(&wavHeader.overall_size, 4);
        wavFile.read(&wavHeader.wave, 4);
        wavFile.read(&wavHeader.fmt_chunk_marker, 4);
        wavFile.read(&wavHeader.length_of_fmt, 4);
        wavFile.read(&wavHeader.format_type, 2);
        wavFile.read(&wavHeader.channels, 2);
        wavFile.read(&wavHeader.sample_rate, 4);
        wavFile.read(&wavHeader.byterate, 4);
        wavFile.read(&wavHeader.block_align, 2);
        wavFile.read(&wavHeader.bits_per_sample, 2);

        char tempText[64];
        PD::setCursor(0, 0);
        // RIFF
        sprintf(tempText,"(1-4):%s", wavHeader.riff);
    	tempText[10]=0;
    	PD::println(tempText);
        // Size of audio
        sprintf(tempText,"(5-8) Size:%db, %dkb", wavHeader.overall_size, wavHeader.overall_size/1024);
    	PD::println(tempText);
    
        sprintf(tempText,"(9-12) Wave marker:%s", wavHeader.wave);
    	tempText[23]=0;
    	PD::println(tempText);
    
        sprintf(tempText,"(13-16) Fmt marker:%s", wavHeader.fmt_chunk_marker);
    	tempText[22]=0;
    	PD::println(tempText);
    
        sprintf(tempText,"(17-20) Length of Fmt: %u", wavHeader.length_of_fmt);
    	PD::println(tempText);
    
        // wavHeader.format_type
        char format_name[10] = "";
        if (wavHeader.format_type == 1)
            strcpy(format_name,"PCM"); 
        else if (wavHeader.format_type == 6)
            strcpy(format_name, "A-law");
        else if (wavHeader.format_type == 7)
            strcpy(format_name, "Mu-law");
        else if (wavHeader.format_type == 17)
            strcpy(format_name, "ADPCM");
        else
            strcpy(format_name, "Unknown");
        
        sprintf(tempText, "(21-22) Format:%u %s", wavHeader.format_type, format_name);
    	PD::println(tempText);
    
        sprintf(tempText, "(23-24) Channels:%u", wavHeader.channels);
    	PD::println(tempText);
    
        sprintf(tempText, "(25-28) Sample rate:%u", wavHeader.sample_rate);
    	PD::println(tempText);
        
        sprintf(tempText, "(29-32) Byte Rate:%u", wavHeader.byterate);
    	PD::println(tempText);
        sprintf(tempText, "        Bit Rate:%u", wavHeader.byterate*8);
    	PD::println(tempText);
    
        sprintf(tempText, "(33-34) Block Align:%u", wavHeader.block_align);
    	PD::println(tempText);
    
        sprintf(tempText, "(35-36) Bits per sample:%u", wavHeader.bits_per_sample);
    	PD::println(tempText);


    // if file is any other than PCM, theren there IS ALWAYS an extended portion here
    // even if the size is zero

        // 17 - IMA ADPCM, 2 = Microsoft ADPCM
        if(wavHeader.format_type == 17){

            wavFile.read(&extraData.extraDataSize, 2);
            wavFile.read(&extraData.data_size, 2);
            wavFile.read(&extraData.subID, 4);
            wavFile.read(&extraData.subSize, 4);
            // should read then next 'extraData.subSize' number of bytes, but I wont use them, so just skip.
            wavFile.read(&tempText[0], 4);

            // ADPCM file found, so read 'extra data' before main data    
            sprintf(tempText, "Extra Data:%u", extraData.extraDataSize);
    	    PD::println(tempText);
            sprintf(tempText, "Sample Per Block:%u", extraData.data_size);
    	    PD::println(tempText);
            // fact
            sprintf(tempText,"fact?:%s", extraData.subID);
    	    tempText[10]=0;
    	    PD::println(tempText);
            sprintf(tempText, "Block size:%u", extraData.subSize);
    	    PD::println(tempText);
        }

        wavFile.read(&dataHeader.data_chunk_header, 4);
        wavFile.read(&dataHeader.data_size, 4);

        // fact
        sprintf(tempText,"data?:%s", dataHeader.data_chunk_header);
        tempText[10]=0;
        PD::println(tempText);
    
        sprintf(tempText, "Filesize:%u", dataHeader.data_size);
        PD::println(tempText);
    
        int fileDataOffset = wavFile.tell();
        sprintf(tempText, "File Offset:%u", fileDataOffset); // should be 60 at this point.
        PD::println(tempText);

        //audioBufferOffset=0;

        byteCount = 0;

        //wavFile.read(&prevsample, 2);
    	//wavFile.read(&previndex, 2);
        //prevsample = (signed short) prevsample;
/*
    	wavFile.read(&ADPCMBuffer[0], wavHeader.block_align);
        prevsample = (ADPCMBuffer[byteCount++]<<8) | ADPCMBuffer[byteCount++];
        previndex = ADPCMBuffer[byteCount++];
        byteCount++;
*/
        //PD::clear();
        //char tempText[32];
        // looking for BD E4 2A 00
        //sprintf(tempText,"%02x, %02x, %02x, %02x",ADPCMBuffer[0], ADPCMBuffer[1], ADPCMBuffer[2], ADPCMBuffer[3]);
        //PD::println(tempText);

        initTimer(wavHeader.sample_rate);



    }


}

void update() {

    PD::clear();
    char tempText[16];
    sprintf(tempText,"%d, %d, %d, %d",completeBuffer, currentBuffer, byteCount, audioCount);
    PD::println(tempText);

    if( currentBuffer != completeBuffer){
    	wavFile.read(&ADPCMBuffer[wavHeader.block_align*completeBuffer], wavHeader.block_align);
        completeBuffer = currentBuffer;
    }
    
}

