module alpha

go 1.21

require example.com/dep v1.2.3

require (
	example.com/indirect v0.4.0 // indirect
)

replace example.com/dep => ../dep

exclude example.com/old v0.0.1
