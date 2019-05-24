var y = require('../build/Release/yencode');
var _ = require('./_speedbase');

var bufSize = _.bufTarget.length;

var mWorst = new Buffer(bufSize);
var mAvg = _.bufAvg.map(function() {
	return new Buffer(bufSize);
});
var mAvg2x = _.bufAvg2x.map(function() {
	return new Buffer(bufSize);
});
var mBest = new Buffer(bufSize);
var mBest2 = new Buffer(_.size);
mBest2.fill(32);

var lenWorst = y.encodeTo(_.bufWorst, mWorst);
var lenBest = y.encodeTo(_.bufBest, mBest);
var lenAvg = Array(_.bufAvg.length);
var lenAvg2x = Array(_.bufAvg2x.length);
_.bufAvg.forEach(function(buf, i) {
	lenAvg[i] = y.encodeTo(buf, mAvg[i]);
});
_.bufAvg2x.forEach(function(buf, i) {
	lenAvg2x[i] = y.encodeTo(buf, mAvg2x[i]);
});



_.parseArgs('Syntax: node test/speeddec [-a|--average-only] [{-s|--sleep}=msecs(0)] [{-m|--methods}=clean,raw,incr,rawincr]');

console.log('    Test                     Output rate         Read rate   ');

// warmup
if(!_.sleep) {
	mAvg.forEach(function(buf) {
		var p=process.hrtime();
		for(var j=0;j<_.rounds;j+=2) y.decodeTo(buf, _.bufTarget);
		for(var j=0;j<_.rounds;j+=2) y.decodeNntpTo(buf, _.bufTarget);
		for(var j=0;j<_.rounds;j+=2) y.decodeIncr(buf, 0, _.bufTarget);
		for(var j=0;j<_.rounds;j+=2) y.decodeNntpIncr(buf, 0, _.bufTarget);
		var t=process.hrtime(p);
	});
}

setTimeout(function() {
	if(!_.avgOnly) {
		if(_.decMethods.clean) {
			_.run('Clean worst (all escaping)', y.decodeTo.bind(null, mWorst, _.bufTarget), lenWorst);
			_.run('Clean best (min escaping)',  y.decodeTo.bind(null, mBest, _.bufTarget), lenBest);
			_.run('Clean pass (no escaping)',  y.decodeTo.bind(null, mBest2, _.bufTarget));
		}
		if(_.decMethods.raw) {
			_.run('Raw worst', y.decodeNntpTo.bind(null, mWorst, _.bufTarget), lenWorst);
			_.run('Raw best',  y.decodeNntpTo.bind(null, mBest, _.bufTarget), lenBest);
			_.run('Raw pass',  y.decodeNntpTo.bind(null, mBest2, _.bufTarget));
		}
		if(_.decMethods.incr) {
			_.run('Incr worst', y.decodeIncr.bind(null, mWorst, 0, _.bufTarget), lenWorst);
			_.run('Incr best',  y.decodeIncr.bind(null, mBest, 0, _.bufTarget), lenBest);
			_.run('Incr pass',  y.decodeIncr.bind(null, mBest2, 0, _.bufTarget));
		}
		if(_.decMethods.rawincr) {
			_.run('Raw-incr worst', y.decodeNntpIncr.bind(null, mWorst, 0, _.bufTarget), lenWorst);
			_.run('Raw-incr best',  y.decodeNntpIncr.bind(null, mBest, 0, _.bufTarget), lenBest);
			_.run('Raw-incr pass',  y.decodeNntpIncr.bind(null, mBest2, 0, _.bufTarget));
		}
	}
	
	if(_.decMethods.clean)
		mAvg.forEach(function(buf, i) {
			_.run('Clean random ('+i+')',   y.decodeTo.bind(null, buf, _.bufTarget), lenAvg[i]);
		});
	if(_.decMethods.raw)
		mAvg.forEach(function(buf, i) {
			_.run('Raw random ('+i+')',   y.decodeNntpTo.bind(null, buf, _.bufTarget), lenAvg[i]);
		});
	if(_.decMethods.incr)
		mAvg.forEach(function(buf, i) {
			_.run('Incr random ('+i+')',  y.decodeIncr.bind(null, buf, 0, _.bufTarget), lenAvg[i]);
		});
	if(_.decMethods.rawincr)
		mAvg.forEach(function(buf, i) {
			_.run('Raw-incr random ('+i+')',  y.decodeNntpIncr.bind(null, buf, 0, _.bufTarget), lenAvg[i]);
		});
	
	if(!_.avgOnly) {
		if(_.decMethods.clean)
			mAvg2x.forEach(function(buf, i) {
				_.run('Clean random 2xEsc ('+i+')',   y.decodeTo.bind(null, buf, _.bufTarget), lenAvg2x[i]);
			});
		if(_.decMethods.raw)
			mAvg2x.forEach(function(buf, i) {
				_.run('Raw random 2xEsc ('+i+')',   y.decodeNntpTo.bind(null, buf, _.bufTarget), lenAvg2x[i]);
			});
		if(_.decMethods.incr)
			mAvg2x.forEach(function(buf, i) {
				_.run('Incr random 2xEsc ('+i+')',  y.decodeIncr.bind(null, buf, 0, _.bufTarget), lenAvg2x[i]);
			});
		if(_.decMethods.rawincr)
			mAvg2x.forEach(function(buf, i) {
				_.run('Raw-incr random 2xEsc ('+i+')',  y.decodeNntpIncr.bind(null, buf, 0, _.bufTarget), lenAvg2x[i]);
			});
	}
}, _.sleep);
