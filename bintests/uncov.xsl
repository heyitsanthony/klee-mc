<?xml version='1.0'?>
<xsl:stylesheet version="1.0"
                xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
                xmlns="http://www.w3.org/1999/xhtml">

<xsl:output method="xml"
            indent="yes"
            omit-xml-declaration="yes"
            doctype-public = "-//W3C//DTD XHTML 1.0 Strict//EN"
            doctype-system = "http://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd"
/>

<xsl:template name="disasm">
<xsl:param name="tableClass"/>
<table>
	<xsl:attribute name="class"><xsl:value-of select="$tableClass"/></xsl:attribute>
	<xsl:for-each select="ins">
	<xsl:choose>
		<xsl:when test="@active">
			<tr>
			<td class="addr_visited"><xsl:value-of select="addr" /></td>
			<td class="ins_visited"><xsl:value-of select="op" /></td>
			</tr>
		</xsl:when>
		<xsl:otherwise>
			<tr>
			<td class="addr"><xsl:value-of select="addr" /></td>
			<td class="ins"><xsl:value-of select="op" /></td>
			</tr>
		</xsl:otherwise>
	</xsl:choose>
	</xsl:for-each>
</table>
</xsl:template>

<xsl:template name="func-active">
<div class="func-active" onclick="clicky(this,event,1);">
	<span class="funcname-active">
	<xsl:value-of select="name" />
	(<xsl:value-of select="count(ins/@active)" /> / <xsl:value-of select="count(ins)" />)
	</span>
	<xsl:call-template name="disasm">
		<xsl:with-param name="tableClass">disasm-active</xsl:with-param>
	</xsl:call-template>
</div>

</xsl:template>

<xsl:template name="func-inactive">
<div class="func-inactive" onclick="clicky(this,event,1);">
	<span class="funcname-inactive">
	<xsl:value-of select="name" /> (0 / <xsl:value-of select="count(ins)" />)
	</span>
	<xsl:call-template name="disasm">
		<xsl:with-param name="tableClass">disasm-inactive</xsl:with-param>
	</xsl:call-template>
</div>
</xsl:template>


<xsl:template match="/">
<html>
<head>
<link href="uncov.css" rel="stylesheet" type="text/css" />
<title>KLEE-MC AppCoverage</title>
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
	toggleClass('disasm-active', toggleValue);
	toggleClass('disasm-inactive', toggleValue);
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

<body>
<h1>AppCoverage: <xsl:value-of select="appcov/binary" /></h1>

<table>
<tr>
<td width="20em;"><b>Instructions:</b></td>
<td>
<xsl:value-of select="count(appcov/func/ins/@active)" /> / 
<xsl:value-of select="count(appcov/func/ins)" />
</td>
</tr>
<tr>
<td><b>Total Coverage:</b></td>
<td>
 <xsl:value-of select="round(100*count(appcov/func/ins/@active) div count(appcov/func/ins))" />%
</td>
</tr>
</table>

<br/>

<span onclick="togglevis()" style="background-color: orange; width: 20em; display: block; ">Toggle All Functions</span><br/>

<p>
<span onclick="toggleClass('func-inactive', 'block')" style="display: block; width: 20em; background-color: #b0e0e6;">Show Unvisited Functions</span>
<span onclick="toggleClass('func-inactive', 'none')" style="display: block; width: 20em; background-color: chartreuse;">Hide Unvisited Functions</span>
<span onclick="toggleClass('disasm-inactive', 'none')" style="display: block; width: 20em; background-color: yellow;">Hide Unvisited Assembly</span>
</p>

<p>
<span onclick="toggleClass('func-active', 'block')" style="display: block; width: 20em; background-color: #b0e0e6;">Show Visited Functions</span>
<span onclick="toggleClass('func-active', 'none')" style="display: block; width: 20em; background-color: chartreuse;">Hide Visited Functions</span>
<span onclick="toggleClass('disasm-active', 'none')" style="display: block; width: 20em; background-color: yellow;">Hide Visited Assembly</span>
</p>

<hr/>

Functions:
<br/>

<xsl:for-each select="appcov/func">
<xsl:sort select="name" />
<xsl:choose>
<xsl:when test="count(ins/@active)">
	<xsl:call-template name="func-active" />
</xsl:when>
<xsl:otherwise>
	<xsl:call-template name="func-inactive" />
</xsl:otherwise>
</xsl:choose>
</xsl:for-each>

<hr/>
</body>
</html>
</xsl:template>
</xsl:stylesheet>
