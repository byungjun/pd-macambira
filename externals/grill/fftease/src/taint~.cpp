/*

FFTease - A set of Live Spectral Processors
Originally written by Eric Lyon and Christopher Penrose for the Max/MSP platform

Copyright (c)Thomas Grill (xovo@gmx.net)
For information on usage and redistribution, and for a DISCLAIMER OF ALL
WARRANTIES, see the file, "license.txt," in this distribution.  

*/

#include "main.h"
#include <stdlib.h>

class taint:
	public fftease
{
	FLEXT_HEADER_S(taint,fftease,setup)
	
public:
	taint(I argc,const t_atom *argv);

protected:

	virtual V Transform(I n,S *const *in);

	V ms_thresh(F thr) { _threshold = (F)pow(10.,(_threshdB = thr)*.05); }

	F _threshold,_threshdB;
	BL _invert;

private:
	static V setup(t_classid c);

	FLEXT_ATTRGET_F(_threshdB)
	FLEXT_CALLSET_F(ms_thresh)
	FLEXT_ATTRVAR_B(_invert)
};

FLEXT_LIB_DSP_V("fftease, taint~",taint)


V taint::setup(t_classid c)
{
	FLEXT_CADDATTR_VAR(c,"threshold",_threshdB,ms_thresh);
	FLEXT_CADDATTR_VAR1(c,"invert",_invert);
}


taint::taint(I argc,const t_atom *argv):
	fftease(4,F_STEREO|F_WINDOW|F_BITSHUFFLE|F_CONVERT),
	_threshdB(-10),_invert(false)
{
	/* parse and set object's options given */
	if(argc >= 1) {
		if(CanbeFloat(argv[0]))
			_threshdB = GetAFloat(argv[0]);
		else
			post("%s - Threshold must be a float value - set to %f",thisName(),_threshdB);
	}

	if(argc >= 2) {
		if(CanbeBool(argv[1]))
			_invert = GetABool(argv[1]);
		else
			post("%s - Invert must be a boolean value - set to %0i",thisName(),_invert?1:0);
	}

	ms_thresh(_threshdB);

	AddInSignal("Messages and frequency reference signal");
	AddInSignal("Amplitude reference signal");
	AddOutSignal("Transformed signal");
}


V taint::Transform(I _N2,S *const *in)
{
	for (I i = 0; i <= _N2; i++ ) {
		const I even = i*2, odd = even + 1;

		const F magnitude = _channel2[even];

		if(_invert) {
			// use threshold for inverse filtering to avoid division by zero 
			if ( magnitude < _threshold )
				_channel1[even] = 0;
			else
				_channel1[even] /= magnitude;
		}
		else {
			// simple multiplication of magnitudes 
			if (magnitude > _threshold)
				_channel1[even] *= magnitude;
		}
	}
}
