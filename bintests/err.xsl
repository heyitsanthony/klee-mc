<html
 xsl:version="1.0"
 xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
 xmlns="http://www.w3.org/1999/xhtml">
<head>
<title>KLEE-MC /bin/ary tests - Errors</title>
<script language="javascript">
<![CDATA[
function clicky(x,eventHandle,n)
{
	// pop event bubble
	if (!eventHandle) var eventHandle = window.event;
	if (eventHandle) eventHandle.cancelBubble = true;
	if (eventHandle.stopPropagation) eventHandle.stopPropagation();

	var fl = x.childNodes[n];
	if (!fl) return;
	fl.style.display = (fl.style.display == "none") ? "block" : "none";
}


var toggleValue="block";
function togglevis()
{
	toggleClass('extra-info', toggleValue);
	toggleClass('frame-list', toggleValue);
//	toggleClass('constraint-info', toggleValue);
	toggleValue = (toggleValue == 'none') ?  'block' : 'none';
}

function toggleClass(classname, val)
{
	var	elems;
	elems = document.getElementsByClassName(classname);
	for (var i = 0; i < elems.length; i++) {
		var e = elems.item(i);
		if (e.style) e.style.display = val;
	}
}
]]>
</script>
</head>
<body style="font-family:Arial; font-size:12pt; background-image: url('../errbg.gif'); background-repeat: repeat; padding: 2em; margin: 0px; color: white">
<h1>KLEE-MC Errors in /bin/ary tests</h1>

<b>Total Errors: <xsl:value-of select="count(//error)"/></b><br/>
<span onclick="togglevis()"><b>Toggle Visibility</b></span>
<hr/>

<xsl:for-each select="errors/error">
<xsl:sort select="cmdline" />
<p style="margin-top: 1px; margin-bottom: 1px; padding-top: 1px; padding-bottom: 1px;" >
	<div
		style="font-weight:bold; background-color:teal; color:white; padding: 3px 1em 3px 1em; width: 95%;"
		onclick="clicky(this,event,1);">
		<xsl:value-of select="cmdline"/>
		<div id="ei" class="extra-info" style="color: black; display: none; font-size: 10pt;">
		<xsl:value-of select="errfile" /><br/>
		<div id="frame-container" onclick="clicky(this,event,1)"
			style="background-color: chartreuse; color: black; padding: 2px; margin: 3px; border: 1px solid black;"  >Stack
		<div id="fl" class="frame-list" style="display: none; font-family:monospace;">
		<hr/>
		<xsl:for-each select="frames/frame"><xsl:value-of select="." /><br/></xsl:for-each>
		</div>
		</div>
		<div	id="constraint-container"
			style="border: 1px solid black; background-color: plum; color: black; padding: 2px; margin: 3px;"
			onclick="clicky(this,event,1)">
		Constraints
			<div id="ci" class="constraint-info" style="display: none; font-family:monospace;">
			<hr/>
			<pre><xsl:value-of select="constraints" /></pre>
			</div>
		</div>

		</div>
	</div>
</p>
</xsl:for-each>

<hr/>
</body>
</html>