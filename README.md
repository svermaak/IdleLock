IdleLock
========

If you want your Windows PC to lock (i.e. require you to enter your password to use it) 
after a period of user inactivity, you can go to the screen saver setting and check 
"On resume, display logon screen". But what if you want the screensaver to be activated 
after e.g. 10 minutes, but you don't want the PC to lock until after 20 minutes? In 
Windows XP, you could resort to the ScreenSaverGracePeriod registry hack. However, in 
Windows 7 and later, the PC will be locked no later than 60 seconds after the screensaver 
kicks in, no matter what ScreenSaverGracePeriod value you have specified. To remedy this 
flaw, I whipped up IdleLock, a small utility that locks your PC after a selectable time 
of user inactivity.

<a href="http://blog.wezeku.com/2014/03/14/idlelock-a-utility-to-lock-your-pc-after-x-minutes-of-idle-time/" target="_blank">See this blog post for more information.</a>
