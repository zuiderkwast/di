case foo of
	"br\u00e4nnvin" -> 42;
	false -> 0.1;
	/^\w(\d+)/ -> $1
