#
/*
 *    Copyright (C) 2013 .. 2017
 *    Jan van Katwijk (J.vanKatwijk@gmail.com)
 *    Lazy Chair Computing
 *
 *    This file is part of the Qt-DAB program
 *    Qt-DAB is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    Qt-DAB is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with Qt-DAB; if not, write to the Free Software
 *    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#ifndef	__OFDM_DECODER__
#define	__OFDM_DECODER__

#include	"dab-constants.h"
#ifdef	__THREADED_DECODING
#include	<QThread>
#include	<QWaitCondition>
#include	<QMutex>
#include	<QSemaphore>
#include	<atomic>
#else
#include	<QObject>
#endif
#include	<vector>
#include	<stdint.h>
#include	"fft-handler.h"
#include	"ringbuffer.h"
#include	"phasetable.h"
#include	"freq-interleaver.h"
#include	"dab-params.h"

class	RadioInterface;
class	ficHandler;
class	mscHandler;

#ifdef	__THREADED_DECODING
class	ofdmDecoder: public QThread {
#else
class	ofdmDecoder: public QObject {
#endif
Q_OBJECT
public:
		ofdmDecoder		(RadioInterface *,
	                                 uint8_t,
	                                 int16_t,
	                                 ficHandler	*,
	                                 mscHandler	*);
		~ofdmDecoder		(void);
	void	processBlock_0		(std::vector<std::complex<float> >);
	void	decodeFICblock		(std::vector<std::complex<float> >, int32_t n);
	void	decodeMscblock		(std::vector<std::complex<float> >, int32_t n);
	int16_t	get_snr			(std::complex<float> *);
	void	stop			(void);
	void	reset			(void);
#ifndef	__THREADED_DECODING
	void	start			(void);
#endif
private:
	RadioInterface	*myRadioInterface;
	dabParams	params;
	fftHandler	my_fftHandler;
	ficHandler	*my_ficHandler;
	mscHandler	*my_mscHandler;
#ifdef	__THREADED_DECODING
	void		run		(void);
	std::atomic<bool>		running;
	std::complex<float>	**command;
	int16_t		amount;
	int16_t		currentBlock;
	void		processBlock_0		(void);
	void		decodeFICblock		(int32_t n);
	void		decodeMscblock		(int32_t n);
	QSemaphore	bufferSpace;
	QWaitCondition	commandHandler;
	QMutex		helper;
#endif
	int32_t		T_s;
	int32_t		T_u;
	int32_t		T_g;
	int32_t		nrBlocks;
	int32_t		carriers;
	int16_t		getMiddle	(void);
	std::vector<complex<float>>	phaseReference;
	std::vector<int16_t>		ibits;
	std::complex<float>	*fft_buffer;
	interLeaver	myMapper;
	phaseTable	*phasetable;
	int32_t		blockIndex;
	int16_t		snrCount;
	int16_t		snr;
	int16_t		maxSignal;
signals:
	void		show_snr	(int);
};

#endif


