/*
 utility/SamSPI.h - Library for the Motate system
 http://tinkerin.gs/
 
 Copyright (c) 2013 Robert Giseburt
 
 This file is part of the Motate Library.
 
 This file ("the software") is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License, version 2 as published by the
 Free Software Foundation. You should have received a copy of the GNU General Public
 License, version 2 along with the software. If not, see <http://www.gnu.org/licenses/>.
 
 As a special exception, you may use this file as part of a software library without
 restriction. Specifically, if other files instantiate templates or use macros or
 inline functions from this file, or you compile this file and link it with  other
 files to produce an executable, this file does not by itself cause the resulting
 executable to be covered by the GNU General Public License. This exception does not
 however invalidate any other reasons why the executable file might be covered by the
 GNU General Public License.
 
 THE SOFTWARE IS DISTRIBUTED IN THE HOPE THAT IT WILL BE USEFUL, BUT WITHOUT ANY
 WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT
 SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF
 OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 
 */

#ifndef SAMSPI_H_ONCE
#define SAMSPI_H_ONCE

#include "sam.h"

#include "MotatePins.h"
#include "SamCommon.h"

namespace Motate {
    
    // WHOA!! We only support master mode ... for now.
    
    
    enum SPIMode {
        
        kSPIPolarityNormal     = 0,
        kSPIPolarityReversed   = SPI_CSR_CPOL,
        
        // Using the wikipedia deifinition of "normal phase," see:
        //   http://en.wikipedia.org/wiki/Serial_Peripheral_Interface_Bus#Clock_polarity_and_phase
        // Wikipedia, in turn, sites Freescale's SPI Block Guide:
        //   http://www.ee.nmt.edu/~teare/ee308l/datasheets/S12SPIV3.pdf
        
        // This makes the Phase flag INVERTED from that of the SAM3X/A datasheet.
        
        kSPIClockPhaseNormal   = SPI_CSR_NCPHA,
        kSPIClockPhaseReversed = 0,
        
        // Using the wikipedia/freescale mode numbers (and the SAM3X/A datashgeet agrees).
        // The arduino mode settings appear to mirror that of wikipedia as well,
        //  so we should all be in agreement here.
        kSPIMode0              = kSPIPolarityNormal | kSPIClockPhaseNormal,
        kSPIMode1              = kSPIPolarityNormal | kSPIClockPhaseReversed,
        kSPIMode2              = kSPIPolarityReversed | kSPIClockPhaseNormal,
        kSPIMode3              = kSPIPolarityReversed | kSPIClockPhaseReversed,
        
        kSPI8Bit               = SPI_CSR_BITS_8_BIT,
        kSPI9Bit               = SPI_CSR_BITS_9_BIT,
        kSPI10Bit               = SPI_CSR_BITS_10_BIT,
        kSPI11Bit               = SPI_CSR_BITS_11_BIT,
        kSPI12Bit               = SPI_CSR_BITS_12_BIT,
        kSPI13Bit               = SPI_CSR_BITS_13_BIT,
        kSPI14Bit               = SPI_CSR_BITS_14_BIT,
        kSPI15Bit               = SPI_CSR_BITS_15_BIT,
        kSPI16Bit               = SPI_CSR_BITS_16_BIT
	};

    
    // This is an internal representation of the peripheral.
    // This is *not* to be used externally.
	template<const uint8_t spiPeripheralNum>
    struct _SPIHardware {
        
		static Spi * const spi();
        static const uint32_t peripheralId(); // ID_SPI0 .. ID_SPI1
		static const IRQn_Type spiIRQ();
        
        // We'll create a static initilizer object for each SPI peripheral.
        // Since there is only one, the init for each will only be called once.
        struct _SPIHardwareInitializer {
            typedef _SPIHardware<spiPeripheralNum> parent_t;
            
            // shortcut to spi() in the parent. The optimizer should make this free.
            static Spi * const spi() { return parent_t::spi(); };
            
            _SPIHardwareInitializer() {
                pmc_enable_periph_clk( spiIRQ );
                spi()->SPI_CR = SPI_CR_SPIDIS;
                
                /* Execute a software reset of the SPI twice */
                /* Why? Because ATMEL said!  -Rob*/
                spi()->SPI_CR = SPI_CR_SWRST;
                spi()->SPI_CR = SPI_CR_SWRST;
                
                // Set Mode Register to Master mode + Mode Fault Detection Disabled
                spi()->SPI_MR = SPI_MR_MSTR | SPI_MR_MODFDIS;
                
                
                // CONFIGURE PINS!
            };
        };
        
        static _SPIHardwareInitializer _hw_initializer; // <-- simply creating it will do the init.
        
        void enable() {
            //
        };
        
        void disable () {
            //
        };
        
        uint16_t transmit(uint8_t channel, const uint16_t data) {
            
        };
    };
    
	template<const uint8_t spiCSPinNumber>
	struct SPI : SamCommon< SPI<spiCSPinNumber> > {
        
        static const uint8_t spiPeripheralNum();
        static _SPIHardware< SPIChipSelectPin<spiCSPinNumber>::moduleId > hardware;
        static const uint8_t spiChannelNumber() { return SPIChipSelectPin<spiCSPinNumber>::csOffset; };
        
        static Spi * const spi() { return hardware.spi(); };
        static const uint32_t peripheralId() { return hardware.peripheralId(); };
        static const IRQn_Type spiIRQ() { return hardware.spiIRQ(); };
        
        static const SPIChipSelectPin<spiCSPinNumber> csPin;

        typedef SamCommon< SPI<spiCSPinNumber> > common;
        
        SPI(const uint32_t baud = 4000000, const uint16_t options = kNormal) {
            init(baud, options, /*fromConstructor =*/ true);
        };
        
        void init(const uint32_t baud, const uint16_t options = kNormal, const bool fromConstructor=false) {
            common::enablePeripheralClock();
            spi()->SPI_CR = SPI_CR_SPIDIS;
            
            
            /* Execute a software reset of the SPI twice */
            /* Why? Because ATMEL said!  -Rob*/
            spi()->SPI_CR = SPI_CR_SWRST;
            spi()->SPI_CR = SPI_CR_SWRST;
            
            setOptions(baud, options, /*fromConstructor =*/true);
        };
        
        void setOptions(const uint32_t baud, const uint16_t options, const bool fromConstructor=false) {
            // We derive the baud from the master clock with a divider.
            // We want the closest match *below* the value asked for. It's safer to bee too slow.
            
            uint8_t divider = SystemCoreClock / baud;
            if (divider > 255)
                divider = 255;
            else if (divider < 1)
                divider = 1;
            
            // Cruft from Arduino: TODO: Make configurable.
            // SPI_CSR_DLYBCT(1) keeps CS enabled for 32 MCLK after a completed
            // transfer. Some device needs that for working properly.
            spi()->SPI_CSR[spiChannelNumber()] = (options & (SPI_CSR_NCPHA | SPI_CSR_CPOL | SPI_CSR_BITS_Msk)) | SPI_CSR_SCBR(divider) | SPI_CSR_DLYBCT(1);
        };
        
        uint16_t getOptions() {
            return spi()->SPI_CSR[spiChannelNumber()] & (SPI_CSR_NCPHA | SPI_CSR_CPOL | SPI_CSR_BITS_Msk);
        };
        
		int16_t readByte() {
            return hardware.transmit(spiChannelNumber(), 0);
		};
        
		uint16_t read(const uint8_t *buffer, const uint16_t length) {
			int16_t total_read = 0;
			int16_t to_read = length;
			const uint8_t *read_ptr = buffer;
            
			// BLOCKING!!
			while (to_read > 0) {
				// Oddity of english: "to read" and "amount read" makes the same read.
				// So, we'll call it "amount_read".
//				int16_t amount_read = usb.read(read_endpoint, read_ptr, length);
                
//				total_read += amount_read;
//				to_read -= amount_read;
//				read_ptr += amount_read;
			};
            
			return total_read;
		};
        
		int32_t write(const uint8_t *data, const uint16_t length) {
			int16_t total_written = 0;
			int16_t written = 1; // start with a non-zero value
			const uint8_t *out_buffer = data;
			int16_t to_write = length;
            
			// BLOCKING!!
			do {
                //				written = usb.write(write_endpoint, out_buffer, length);
                
				if (written < 0) // ERROR!
                    break;
                
				// TODO: Do this better... -Rob
				total_written += written;
				to_write -= written;
				out_buffer += written;
			} while (to_write);
            
			// HACK! Autoflush forced...
			if (total_written > 0)
                flush();
            
			return total_written;
		}
        
		void flush() {
			// TODO
		}
        
        
	};
    
}

#endif /* end of include guard: SAMSPI_H_ONCE */