#!/usr/bin/python

import smtplib
import glob
import datetime

date = datetime.datetime.now().strftime("%Y-%m-%d")
outputfilename = 'type-punning.log'

logoutput = []

for log in glob.glob('/var/www/auto-build/' + date + '/logs/20*.txt'):
	f = open(log, 'r')
	logoutput.append('======================================================================\n')
	logoutput.append(log + '\n')
 	logoutput.append('======================================================================\n')
	for line in f:
		if line.find('warning: dereferencing type-punned pointer will break strict-aliasing rules') > -1:
			logoutput.append(line.replace('/home/pd/auto-build/', '')
							 .replace('C:/MinGW/msys/1.0', '')
							 .replace('/Users/pd/auto-build/', ''))
	f.close()

lf = open('/tmp/'+outputfilename, 'w')
for line in logoutput:
	lf.write(line)
lf.close()

# make the email report
fromaddr = 'pd@pdlab.idmi.poly.edu'
toaddr = 'hans@at.or.at'
mailoutput = []
mailoutput.append('From: ' + fromaddr + '\n')
mailoutput.append('To: ' + toaddr + '\n')
mailoutput.append('Subject: type-punning warnings ' + date + '\n\n\n')
mailoutput.append('______________________________________________________________________\n\n')
mailoutput.append('Complete log:\n')
mailoutput.append('http://autobuild.puredata.info/auto-build/' + date + '/'
                  + outputfilename + '\n')


# upload the log file to the autobuild website
rsyncfile = 'rsync://128.238.56.50/upload/' + date + '/logs/' + outputfilename
try:
    p = subprocess.Popen(['rsync', '-ax', '/tmp/'+outputfilename, rsyncfile],
                         stdout=subprocess.PIPE, stderr=subprocess.STDOUT).wait()
except:
    mailoutput.append('rsync upload of the log failed!\n')
#    mailoutput.append(''.join(p.stdout.readlines()))



mailoutput.append('______________________________________________________________________\n\n')
server = smtplib.SMTP('in1.smtp.messagingengine.com')
server.sendmail(fromaddr, toaddr, ''.join(mailoutput + logoutput))
server.quit()
